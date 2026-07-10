#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "config/Config.h"
#include "config/ScannerConfig.h"
#include "core/ArbitrageDetector.h"
#include "core/CompositeOpportunitySink.h"
#include "core/OpportunitySink.h"
#include "core/QuoteStore.h"
#include "core/RedisOpportunitySink.h"
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
        // Every new venue is one more entry here — nothing else in this file
        // depends on which or how many venues are configured.
        std::vector<std::unique_ptr<IMarketDataConnector>> connectors;
        connectors.push_back(std::make_unique<BinanceMarketDataConnector>());
        connectors.push_back(std::make_unique<BybitMarketDataConnector>());
        connectors.push_back(std::make_unique<KucoinSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<KucoinFutMarketDataConnector>());
        connectors.push_back(std::make_unique<BitgetSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<BitgetFutMarketDataConnector>());
        connectors.push_back(std::make_unique<HtxSpotMarketDataConnector>());
        connectors.push_back(std::make_unique<HtxFutMarketDataConnector>());

        std::vector<std::string> venues;
        for (const auto& connector : connectors) venues.push_back(connector->exchangeName());

        DatabaseRepository repository(Config::postgresConnectionString());
        const auto symbols = repository.loadSymbolsActiveOnVenues(venues);
        std::cout << "Tracking " << symbols.size() << " symbols active on all of:";
        for (const auto& venue : venues) std::cout << " " << venue;
        std::cout << "\n";

        QuoteStore store;
        std::vector<std::atomic<long>> quoteCounts(connectors.size());

        for (size_t i = 0; i < connectors.size(); ++i) {
            connectors[i]->subscribe(symbols);
            connectors[i]->setOnQuote([&store, &quoteCounts, i](const Quote& quote) {
                store.update(quote);
                ++quoteCounts[i];
            });
        }

        std::vector<std::shared_ptr<OpportunitySink>> sinks{std::make_shared<StdoutOpportunitySink>()};
        try {
            sinks.push_back(std::make_shared<RedisOpportunitySink>(
                ScannerConfig::redisHost(), ScannerConfig::redisPort(), ScannerConfig::redisChannel()));
            std::cout << "Publishing opportunities to Redis channel '" << ScannerConfig::redisChannel()
                      << "'\n";
        } catch (const std::exception& ex) {
            std::cerr << "Redis sink disabled: " << ex.what() << "\n";
        }
        auto sink = std::make_shared<CompositeOpportunitySink>(std::move(sinks));

        ArbitrageDetector detector(store, symbols, venues, sink, ScannerConfig::quoteTtl(),
                                    ScannerConfig::detectionInterval(), ScannerConfig::feeBufferBps(),
                                    ScannerConfig::slippageBufferBps(),
                                    ScannerConfig::minNetSpreadBps());

        for (const auto& connector : connectors) connector->start();
        detector.start();

        auto lastReport = std::chrono::steady_clock::now();
        while (!shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (std::chrono::steady_clock::now() - lastReport > std::chrono::seconds(5)) {
                std::cerr << "quotes received:";
                for (size_t i = 0; i < connectors.size(); ++i) {
                    std::cerr << " " << venues[i] << "=" << quoteCounts[i];
                }
                std::cerr << "\n";
                lastReport = std::chrono::steady_clock::now();
            }
        }

        std::cout << "Shutting down...\n";
        detector.stop();
        for (auto& connector : connectors) connector->stop();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
