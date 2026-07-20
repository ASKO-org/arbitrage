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

#include "config/Config.h"
#include "config/MarketDataFeedConfig.h"
#include "database/DatabaseRepository.h"
#include "marketdata/BinanceFutMarketDataConnector.h"
#include "marketdata/BinanceMarketDataConnector.h"
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
    "BINANCE_Fut", "BYBIT_Spot", "BYBIT_Fut", "OKX_Spot", "OKX_Fut", "GATEIO_Spot", "GATEIO_Fut",
};

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
        connectors.push_back(std::make_unique<BinanceMarketDataConnector>());
        connectors.push_back(std::make_unique<BybitMarketDataConnector>());
        connectors.push_back(std::make_unique<KucoinSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<KucoinFutMarketDataConnector>());
        connectors.push_back(std::make_unique<BitgetSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<BitgetFutMarketDataConnector>());
        connectors.push_back(std::make_unique<HtxSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<HtxFutMarketDataConnector>());
        connectors.push_back(std::make_unique<OkxSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<OkxFutMarketDataConnector>());
        connectors.push_back(std::make_unique<BinanceFutMarketDataConnector>());
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

        QuoteFeedPublisher publisher(MarketDataFeedConfig::redisHost(), MarketDataFeedConfig::redisPort(),
                                      MarketDataFeedConfig::quoteChannel());
        std::cout << "Publishing quotes to Redis channel '" << MarketDataFeedConfig::quoteChannel()
                   << "'\n";

        std::vector<std::atomic<long>> quoteCounts(connectors.size());
        std::vector<LatencyAccumulator> latencies(connectors.size());
        std::mutex publishMutex;  // redisContext isn't safe for concurrent use across connector IO threads

        for (size_t i = 0; i < connectors.size(); ++i) {
            connectors[i]->subscribe(symbols);
            connectors[i]->setOnQuote([&publisher, &quoteCounts, &latencies, &publishMutex, i](const Quote& quote) {
                const auto latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now() - quote.exchangeTimestamp)
                                           .count();
                latencies[i].record(latencyMs);
                ++quoteCounts[i];
                std::lock_guard<std::mutex> lock(publishMutex);
                publisher.publish(quote);
            });
        }

        for (const auto& connector : connectors) connector->start();

        auto lastReport = std::chrono::steady_clock::now();
        const auto reportInterval = MarketDataFeedConfig::statsReportInterval();
        while (!shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
