#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "core/QuoteStore.h"
#include "database/DatabaseRepository.h"
#include "models/TrackedSymbol.h"

// Periodically snapshots the current best bid/ask for every tracked symbol
// across every venue in its map, and batch-persists them to Postgres.
class QuoteSnapshotWriter {
public:
    QuoteSnapshotWriter(const QuoteStore& store, std::vector<TrackedSymbol> symbols,
                         DatabaseRepository& repository, std::chrono::milliseconds quoteTtl,
                         std::chrono::milliseconds snapshotInterval);
    ~QuoteSnapshotWriter();

    void start();
    void stop();

private:
    void run();
    void takeSnapshot();

    const QuoteStore& store_;
    std::vector<TrackedSymbol> symbols_;
    DatabaseRepository& repository_;
    std::chrono::milliseconds quoteTtl_;
    std::chrono::milliseconds snapshotInterval_;

    std::atomic<bool> running_{false};
    std::thread thread_;
};
