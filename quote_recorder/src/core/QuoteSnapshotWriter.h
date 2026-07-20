#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include "core/QuoteSnapshotFileWriter.h"
#include "core/QuoteStore.h"
#include "database/DatabaseRepository.h"
#include "models/TrackedSymbol.h"

// Periodically snapshots the current best bid/ask for every tracked symbol
// across every venue in its map, and appends them to per-exchange, per-day
// binary snapshot files (see QuoteSnapshotFileWriter). A separate, much
// slower cadence still reports measured update rates to Postgres.
class QuoteSnapshotWriter {
public:
    QuoteSnapshotWriter(const QuoteStore& store, std::vector<TrackedSymbol> symbols,
                         DatabaseRepository& repository, std::chrono::milliseconds quoteTtl,
                         std::chrono::milliseconds snapshotInterval, std::string snapshotDir,
                         std::size_t snapshotFlushThreshold);
    ~QuoteSnapshotWriter();

    void start();
    void stop();

private:
    void run();
    void takeSnapshot();
    void reportUpdateRatesIfDue();

    const QuoteStore& store_;
    std::vector<TrackedSymbol> symbols_;
    DatabaseRepository& repository_;
    std::chrono::milliseconds quoteTtl_;
    std::chrono::milliseconds snapshotInterval_;
    QuoteSnapshotFileWriter fileWriter_;

    // Independent, much slower cadence for reporting measured exchange
    // update rates (see QuoteStore::snapshotAndResetCounts) — reporting on
    // every snapshotInterval_ wake (as fast as 200ms) would be noisy and
    // pointless; this is meant to show a stable recent rate.
    static constexpr std::chrono::milliseconds kUpdateRateReportInterval{15000};
    std::chrono::steady_clock::time_point lastUpdateRateReport_{};

    std::atomic<bool> running_{false};
    std::thread thread_;
};
