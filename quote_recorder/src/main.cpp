#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "config/Config.h"
#include "config/RecorderConfig.h"
#include "core/QuoteSnapshotWriter.h"
#include "core/QuoteStore.h"
#include "database/DatabaseRepository.h"
#include "marketdata/BinanceMarketDataConnector.h"
#include "marketdata/BitgetFutMarketDataConnector.h"
#include "marketdata/BitgetSpotMarketDataConnector.h"
#include "marketdata/BybitMarketDataConnector.h"
#include "marketdata/HtxFutMarketDataConnector.h"
#include "marketdata/HtxSpotMarketDataConnector.h"
#include "marketdata/IMarketDataConnector.h"
#include "marketdata/KucoinFutMarketDataConnector.h"
#include "marketdata/KucoinSpotMarketDataConnector.h"

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

        std::vector<std::string> connectedVenues;
        for (const auto& connector : connectors) connectedVenues.push_back(connector->exchangeName());

        const auto symbols = repository.loadWatchlistSymbols();
        std::cout << "Watchlist: " << symbols.size() << " symbols\n";

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
                                    RecorderConfig::snapshotInterval());

        for (const auto& connector : connectors) connector->start();
        writer.start();

        while (!shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "Shutting down...\n";
        writer.stop();
        for (auto& connector : connectors) connector->stop();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
