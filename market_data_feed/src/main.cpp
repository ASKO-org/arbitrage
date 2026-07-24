#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

#include <hiredis/hiredis.h>

#include "config/Config.h"
#include "config/MarketDataFeedConfig.h"
#include "database/DatabaseRepository.h"
#include "marketdata/BingxFutMarketDataConnector.h"
#include "marketdata/BingxSpotMarketDataConnector.h"
#include "marketdata/BitgetFutMarketDataConnector.h"
#include "marketdata/BitgetSpotMarketDataConnector.h"
#include "marketdata/BybitFutMarketDataConnector.h"
#include "marketdata/BybitMarketDataConnector.h"
#include "marketdata/CoinexFutMarketDataConnector.h"
#include "marketdata/CoinexSpotMarketDataConnector.h"
#include "marketdata/GateioFutMarketDataConnector.h"
#include "marketdata/GateioSpotMarketDataConnector.h"
#include "marketdata/HtxFutMarketDataConnector.h"
#include "marketdata/HtxSpotMarketDataConnector.h"
#include "marketdata/IMarketDataConnector.h"
#include "marketdata/KrakenFutMarketDataConnector.h"
#include "marketdata/KrakenSpotMarketDataConnector.h"
#include "marketdata/KucoinFutMarketDataConnector.h"
#include "marketdata/KucoinSpotMarketDataConnector.h"
#include "marketdata/MexcFutMarketDataConnector.h"
#include "marketdata/MexcSpotMarketDataConnector.h"
#include "marketdata/OkxFutMarketDataConnector.h"
#include "marketdata/OkxSpotMarketDataConnector.h"
#include "marketdata/QuoteFeedPublisher.h"

namespace {
std::atomic<bool> shutdownRequested{false};
void handleSignal(int) { shutdownRequested = true; }

// Venues where handleMessage() now parses the exchange's own event
// timestamp out of the payload (see the connector .cpp files) rather than
// stamping Quote::exchangeTimestamp with local receipt time. Latency
// reported for every other venue is receipt-to-publish overhead only
// (expected to sit near zero), not real wire latency — flagged in the
// periodic report below so the numbers aren't mistaken for more than they
// are until the rest of the connectors get the same treatment.
const std::unordered_set<std::string> kVenuesWithNativeTimestamp{
    "BYBIT_Spot", "BYBIT_Fut", "OKX_Spot", "OKX_Fut", "GATEIO_Spot", "GATEIO_Fut",
};

using SteadyClock = std::chrono::steady_clock;

// If a venue goes this long without delivering anything at all, its
// WebSocket connection is almost certainly dead without ever having fired
// its own onClose/onError callback — the exact same silent-death pattern
// fixed in QuoteFeedSubscriber, just one layer up (the exchange connector
// itself, not the Redis subscriber reading from what it publishes). A
// dead connector here means quote_recorder's per-venue subscriber keeps
// correctly reconnecting to a publisher that has nothing to send it,
// which can never resolve on its own — this is what actually needs to
// notice and recover. Forces a full stop()+start() cycle (confirmed safe
// to repeat: subscribe() only stores symbol data, start() is what opens
// the connection and re-subscribes from that stored data) rather than
// waiting for the exchange or IXWebSocket to notice on their own.
constexpr auto kConnectorSilenceTimeout = std::chrono::seconds(30);

// Accumulates exchange-to-publish latency (ms) for one venue between
// periodic reports. Lock-free: recorded from whichever connector IO thread
// receives a quote, read/reset from the single reporting thread.
struct LatencyAccumulator {
    std::atomic<long long> sumMs{0};
    std::atomic<long> count{0};
    std::atomic<long long> minMs{std::numeric_limits<long long>::max()};
    std::atomic<long long> maxMs{std::numeric_limits<long long>::min()};

    void record(long long ms) {
        sumMs.fetch_add(ms, std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);
        for (long long current = minMs.load(std::memory_order_relaxed);
             ms < current && !minMs.compare_exchange_weak(current, ms, std::memory_order_relaxed);) {
        }
        for (long long current = maxMs.load(std::memory_order_relaxed);
             ms > current && !maxMs.compare_exchange_weak(current, ms, std::memory_order_relaxed);) {
        }
    }

    struct Snapshot {
        double avgMs = 0.0;
        long long minMs = 0;
        long long maxMs = 0;
        long count = 0;
    };

    Snapshot takeAndReset() {
        const long long sum = sumMs.exchange(0, std::memory_order_relaxed);
        const long c = count.exchange(0, std::memory_order_relaxed);
        const long long mn = minMs.exchange(std::numeric_limits<long long>::max(), std::memory_order_relaxed);
        const long long mx = maxMs.exchange(std::numeric_limits<long long>::min(), std::memory_order_relaxed);
        Snapshot snapshot;
        snapshot.count = c;
        if (c > 0) {
            snapshot.avgMs = static_cast<double>(sum) / static_cast<double>(c);
            snapshot.minMs = mn;
            snapshot.maxMs = mx;
        }
        return snapshot;
    }
};

// Writes one venue's latest stats to a Redis hash `market_data:stats:<venue>`
// so any process (recorder_viewer, redis-cli, a health check) can see live
// per-exchange throughput/latency without tailing this process's log — and,
// crucially, so a wedged or dead market_data_feed is visible as *stale*
// data rather than invisible. The key TTL is set to a few report intervals:
// if this process stops updating it, the key simply expires and disappears,
// which is itself the "this venue/feed is no longer reporting" signal.
void publishVenueStats(redisContext* ctx, const std::string& venue, long quotesTotal,
                        const LatencyAccumulator::Snapshot& latency, bool nativeTimestamp,
                        std::chrono::seconds ttl) {
    if (!ctx) return;
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const std::string key = "market_data:stats:" + venue;
    auto* hsetReply = static_cast<redisReply*>(redisCommand(
        ctx,
        "HSET %s quotes_total %ld quotes_interval %ld latency_avg_ms %f latency_min_ms %lld "
        "latency_max_ms %lld native_timestamp %d updated_at_ms %lld",
        key.c_str(), quotesTotal, latency.count, latency.avgMs, static_cast<long long>(latency.minMs),
        static_cast<long long>(latency.maxMs), nativeTimestamp ? 1 : 0, static_cast<long long>(nowMs)));
    if (hsetReply) freeReplyObject(hsetReply);

    auto* expireReply = static_cast<redisReply*>(
        redisCommand(ctx, "EXPIRE %s %lld", key.c_str(), static_cast<long long>(ttl.count())));
    if (expireReply) freeReplyObject(expireReply);
}

}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    try {
        DatabaseRepository repository(Config::postgresConnectionString());
        repository.ensureSchema();

        // Every venue with a live WebSocket connector; more slot in here as
        // they're built. Kept as the single source of truth for "which
        // venues exist" — quote_recorder no longer builds this list itself.
        std::vector<std::unique_ptr<IMarketDataConnector>> connectors;
        connectors.push_back(std::make_unique<BybitMarketDataConnector>());
        connectors.push_back(std::make_unique<KucoinSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<KucoinFutMarketDataConnector>());
        connectors.push_back(std::make_unique<BitgetSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<BitgetFutMarketDataConnector>());
        connectors.push_back(std::make_unique<HtxSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<HtxFutMarketDataConnector>());
        connectors.push_back(std::make_unique<OkxSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<OkxFutMarketDataConnector>());
        connectors.push_back(std::make_unique<BybitFutMarketDataConnector>());
        connectors.push_back(std::make_unique<KrakenSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<KrakenFutMarketDataConnector>());
        connectors.push_back(std::make_unique<CoinexSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<CoinexFutMarketDataConnector>());
        connectors.push_back(std::make_unique<GateioSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<GateioFutMarketDataConnector>());
        connectors.push_back(std::make_unique<MexcSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<MexcFutMarketDataConnector>());
        connectors.push_back(std::make_unique<BingxSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<BingxFutMarketDataConnector>());

        const auto symbols = repository.loadWatchlistSymbols();
        std::cout << "Watchlist: " << symbols.size() << " symbols\n";

        std::set<std::string> neededVenues;
        for (const auto& symbol : symbols) {
            for (const auto& [venue, nativeSymbol] : symbol.nativeSymbols) neededVenues.insert(venue);
        }

        // Same rationale as quote_recorder's old filtering: a connector for
        // a venue nothing in the watchlist references just burns CPU/
        // bandwidth reconnecting for data nobody wants, and several venues
        // are prone to disconnect loops when left idle with zero
        // subscriptions.
        connectors.erase(std::remove_if(connectors.begin(), connectors.end(),
                                         [&neededVenues](const auto& connector) {
                                             return neededVenues.find(connector->exchangeName()) ==
                                                    neededVenues.end();
                                         }),
                          connectors.end());

        std::cout << "Connecting to " << connectors.size() << " venues:";
        for (const auto& connector : connectors) std::cout << " " << connector->exchangeName();
        std::cout << "\n";

        // One publisher (own Redis connection) per connector, not one shared
        // connection behind a mutex. A shared connection meant every
        // connector's I/O thread serialized through a single lock for every
        // quote — if Redis was ever briefly slow to respond (e.g. busy
        // freeing a disconnected slow subscriber's output buffer), *all* 12
        // threads stalled together waiting on that lock, which stalled their
        // WebSocket read loops, which triggered exchange-side keepalive
        // timeouts and a disconnect storm. That pattern (climbing latency
        // across venues, then a crash) is exactly what took this process
        // down twice. Independent connections mean one venue's Redis
        // hiccup can no longer stall the other 11.
        // Each connector also publishes to its own per-venue channel (see
        // quoteChannelForVenue in Quote.h) rather than one shared channel —
        // lets quote_recorder parallelize consumption one thread per venue
        // instead of being bottlenecked on a single thread for the combined
        // rate across all 12.
        std::vector<std::unique_ptr<QuoteFeedPublisher>> publishers;
        publishers.reserve(connectors.size());
        for (size_t i = 0; i < connectors.size(); ++i) {
            publishers.push_back(std::make_unique<QuoteFeedPublisher>(
                MarketDataFeedConfig::redisHost(), MarketDataFeedConfig::redisPort(),
                quoteChannelForVenue(MarketDataFeedConfig::quoteChannel(), connectors[i]->exchangeName())));
        }
        std::cout << "Publishing quotes to " << publishers.size()
                   << " per-venue Redis channels (base '" << MarketDataFeedConfig::quoteChannel() << "')\n";

        // Separate connection from the publisher, used only by the report
        // loop below (single-threaded, so no locking needed) — kept
        // independent of the per-quote publish path so a stats hiccup can
        // never contend with or block the hot path.
        std::unique_ptr<redisContext, void (*)(redisContext*)> statsContext(
            redisConnect(MarketDataFeedConfig::redisHost().c_str(), MarketDataFeedConfig::redisPort()),
            [](redisContext* c) {
                if (c) redisFree(c);
            });
        if (!statsContext || statsContext->err) {
            std::cerr << "Stats Redis connection failed; per-exchange stats keys won't be published\n";
            statsContext.reset();
        }

        std::vector<std::atomic<long>> quoteCounts(connectors.size());
        std::vector<LatencyAccumulator> latencies(connectors.size());

        // Updated on every message received (regardless of content), read
        // by the silence watchdog in the main loop below. reconnecting[i]
        // guards against spawning overlapping reconnect attempts for the
        // same connector while one is still in progress.
        std::vector<std::atomic<SteadyClock::rep>> lastMessageAt(connectors.size());
        std::vector<std::atomic<bool>> reconnecting(connectors.size());
        for (auto& t : lastMessageAt) t.store(SteadyClock::now().time_since_epoch().count());

        for (size_t i = 0; i < connectors.size(); ++i) {
            connectors[i]->subscribe(symbols);
            QuoteFeedPublisher* publisher = publishers[i].get();
            connectors[i]->setOnQuote([publisher, &quoteCounts, &latencies, &lastMessageAt, i](
                                           const Quote& quote) {
                const auto latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now() - quote.exchangeTimestamp)
                                           .count();
                latencies[i].record(latencyMs);
                ++quoteCounts[i];
                lastMessageAt[i].store(SteadyClock::now().time_since_epoch().count(),
                                        std::memory_order_relaxed);
                publisher->publish(quote);
            });
        }

        for (const auto& connector : connectors) connector->start();

        auto lastReport = std::chrono::steady_clock::now();
        const auto reportInterval = MarketDataFeedConfig::statsReportInterval();
        while (!shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            for (size_t i = 0; i < connectors.size(); ++i) {
                const auto lastMs = lastMessageAt[i].load(std::memory_order_relaxed);
                const auto silence =
                    SteadyClock::now() - SteadyClock::time_point(SteadyClock::duration(lastMs));
                if (silence <= kConnectorSilenceTimeout) continue;
                if (reconnecting[i].exchange(true)) continue;  // already reconnecting this one

                IMarketDataConnector* connector = connectors[i].get();
                const std::string venue = connector->exchangeName();
                const auto silenceMs = std::chrono::duration_cast<std::chrono::milliseconds>(silence).count();
                std::cerr << "[" << venue << "] no data at all for " << silenceMs
                           << "ms -- connection is almost certainly dead; forcing stop()+start()\n";
                std::thread([connector, &lastMessageAt, &reconnecting, i, venue] {
                    connector->stop();
                    connector->start();
                    lastMessageAt[i].store(SteadyClock::now().time_since_epoch().count(),
                                            std::memory_order_relaxed);
                    reconnecting[i].store(false);
                    std::cerr << "[" << venue << "] restarted after silence\n";
                }).detach();
            }

            if (std::chrono::steady_clock::now() - lastReport > reportInterval) {
                for (size_t i = 0; i < connectors.size(); ++i) {
                    const std::string& venue = connectors[i]->exchangeName();
                    const auto latency = latencies[i].takeAndReset();
                    const bool nativeTs = kVenuesWithNativeTimestamp.count(venue) > 0;
                    std::cerr << venue << ": " << quoteCounts[i] << " quotes";
                    if (latency.count > 0) {
                        std::cerr << ", latency avg/min/max=" << latency.avgMs << "/" << latency.minMs
                                   << "/" << latency.maxMs << "ms"
                                   << (nativeTs ? " (exchange->us)" : " (receipt-stamped, not wire latency)");
                    }
                    std::cerr << "\n";
                    publishVenueStats(statsContext.get(), venue, quoteCounts[i].load(), latency, nativeTs,
                                       reportInterval * 3);
                }
                lastReport = std::chrono::steady_clock::now();
            }
        }

        std::cout << "Shutting down...\n";

        // Stopped concurrently, not sequentially — see quote_recorder's
        // main.cpp for why (each connector's close handshake takes real
        // time; doing 22 sequentially turns "the slowest one" into "the sum
        // of every one").
        std::vector<std::thread> stopThreads;
        stopThreads.reserve(connectors.size());
        for (auto& connector : connectors) {
            stopThreads.emplace_back([&connector] { connector->stop(); });
        }
        for (auto& stopThread : stopThreads) stopThread.join();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
