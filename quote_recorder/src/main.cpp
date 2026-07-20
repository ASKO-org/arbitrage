#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include "config/Config.h"
#include "config/RecorderConfig.h"
#include "core/QuoteSnapshotWriter.h"
#include "core/QuoteStore.h"
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

namespace {
std::atomic<bool> shutdownRequested{false};
void handleSignal(int) { shutdownRequested = true; }
}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    try {
        DatabaseRepository repository(Config::postgresConnectionString());
        repository.ensureSchema();

        // Every venue with a live WebSocket connector; more slot in here as
        // they're built.
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

        // Drop connectors for exchanges the current watchlist doesn't
        // reference at all — starting them just burns CPU/bandwidth
        // reconnecting for data nobody's asking for, and at least a few
        // exchanges (observed: MEXC_Spot, KRAKEN_Spot, BINGX_Spot) are prone
        // to repeated disconnects when left idle with zero subscriptions.
        connectors.erase(std::remove_if(connectors.begin(), connectors.end(),
                                         [&neededVenues](const auto& connector) {
                                             return neededVenues.find(connector->exchangeName()) ==
                                                    neededVenues.end();
                                         }),
                          connectors.end());

        std::vector<std::string> connectedVenues;
        for (const auto& connector : connectors) connectedVenues.push_back(connector->exchangeName());

        size_t coveredCount = 0;
        for (const auto& symbol : symbols) {
            for (const auto& [venue, nativeSymbol] : symbol.nativeSymbols) {
                const bool hasConnector = std::find(connectedVenues.begin(), connectedVenues.end(),
                                                     venue) != connectedVenues.end();
                if (hasConnector) {
                    ++coveredCount;
                } else {
                    std::cerr << "  no live connector yet for " << venue << " (" << symbol.symbolCode
                              << "), will not be recorded until one exists\n";
                }
            }
        }
        std::cout << "  " << coveredCount << " (venue, symbol) pairs have live coverage\n";

        QuoteStore store;
        for (const auto& connector : connectors) {
            connector->subscribe(symbols);
            connector->setOnQuote([&store](const Quote& quote) { store.update(quote); });
        }

        QuoteSnapshotWriter writer(store, symbols, repository, RecorderConfig::quoteTtl(),
                                    RecorderConfig::snapshotInterval(), RecorderConfig::snapshotDir(),
                                    RecorderConfig::snapshotFlushThreshold());

        for (const auto& connector : connectors) connector->start();
        writer.start();

        while (!shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "Shutting down...\n";
        writer.stop();

        // Stopped concurrently, not sequentially — each connector's
        // WebSocket close handshake (and, for KuCoin, its heartbeat thread)
        // takes real time, and doing all 22 one after another turns "the
        // slowest one" into "the sum of every one," which is what made
        // shutdown look hung rather than just slow.
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
