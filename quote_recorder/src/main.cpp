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

        std::set<std::string> neededVenues;
        for (const auto& symbol : symbols) {
            for (const auto& [venue, nativeSymbol] : symbol.nativeSymbols) neededVenues.insert(venue);
        }

        // One subscriber thread per venue, each on its own Redis connection
        // to that venue's own channel (see quoteChannelForVenue in
        // Quote.h) — not one shared channel/thread for the combined feed.
        // Measured combined rate across venues is ~13,000 quotes/sec; a
        // single thread doing JSON-parse-then-store-update can't sustain
        // that, falls permanently behind, and Redis eventually disconnects
        // it as a slow subscriber. Splitting by venue gives each thread
        // only its own share (~1,000/sec), which is comfortably
        // sustainable. QuoteStore::update() is already mutex-protected, so
        // concurrent calls from these threads are safe.
        QuoteStore store;
        std::vector<std::unique_ptr<QuoteFeedSubscriber>> subscribers;
        std::vector<std::thread> subscriberThreads;
        for (const auto& venue : neededVenues) {
            const std::string channel = quoteChannelForVenue(RecorderConfig::quoteChannel(), venue);
            subscribers.push_back(std::make_unique<QuoteFeedSubscriber>(
                RecorderConfig::redisHost(), RecorderConfig::redisPort(), channel));
            QuoteFeedSubscriber* subscriber = subscribers.back().get();
            subscriberThreads.emplace_back([subscriber, &store] {
                subscriber->run([&store](const Quote& quote) { store.update(quote); },
                                 [] { return shutdownRequested.load(); });
            });
        }
        std::cout << "Subscribed to " << subscribers.size() << " per-venue Redis channels\n";

        QuoteSnapshotWriter writer(store, symbols, repository, RecorderConfig::quoteTtl(),
                                    RecorderConfig::snapshotInterval(), RecorderConfig::snapshotDir(),
                                    RecorderConfig::snapshotFlushThreshold());
        writer.start();

        while (!shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "Shutting down...\n";
        writer.stop();
        for (auto& subscriberThread : subscriberThreads) subscriberThread.join();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
