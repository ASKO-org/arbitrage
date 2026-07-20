#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "config/Config.h"
#include "config/RecorderConfig.h"
#include "core/QuoteSnapshotWriter.h"
#include "core/QuoteStore.h"
#include "database/DatabaseRepository.h"
#include "marketdata/QuoteFeedSubscriber.h"

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

        const auto symbols = repository.loadWatchlistSymbols();
        std::cout << "Watchlist: " << symbols.size() << " symbols\n";

        // Quotes now arrive from market_data_feed over Redis pub/sub instead
        // of this process owning WebSocket connectors directly — connectors
        // (and the "which venues does the watchlist need" filtering) live
        // there now, shared with any other consumer that subscribes to the
        // same channel.
        QuoteStore store;
        QuoteFeedSubscriber subscriber(RecorderConfig::redisHost(), RecorderConfig::redisPort(),
                                        RecorderConfig::quoteChannel());
        std::cout << "Subscribed to Redis channel '" << RecorderConfig::quoteChannel() << "'\n";

        std::thread subscriberThread([&subscriber, &store] {
            subscriber.run([&store](const Quote& quote) { store.update(quote); },
                           [] { return shutdownRequested.load(); });
        });

        QuoteSnapshotWriter writer(store, symbols, repository, RecorderConfig::quoteTtl(),
                                    RecorderConfig::snapshotInterval(), RecorderConfig::snapshotDir(),
                                    RecorderConfig::snapshotFlushThreshold());
        writer.start();

        while (!shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "Shutting down...\n";
        writer.stop();
        subscriberThread.join();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
