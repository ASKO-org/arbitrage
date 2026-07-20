#include "core/QuoteSnapshotWriter.h"

#include <iostream>

#include "models/QuoteSnapshotRow.h"
#include "models/QuoteUpdateStatRow.h"

QuoteSnapshotWriter::QuoteSnapshotWriter(const QuoteStore& store, std::vector<TrackedSymbol> symbols,
                                          DatabaseRepository& repository,
                                          std::chrono::milliseconds quoteTtl,
                                          std::chrono::milliseconds snapshotInterval,
                                          std::string snapshotDir,
                                          std::size_t snapshotFlushThreshold)
    : store_(store),
      symbols_(std::move(symbols)),
      repository_(repository),
      quoteTtl_(quoteTtl),
      snapshotInterval_(snapshotInterval),
      fileWriter_(std::move(snapshotDir), snapshotFlushThreshold),
      lastUpdateRateReport_(std::chrono::steady_clock::now()) {}

QuoteSnapshotWriter::~QuoteSnapshotWriter() { stop(); }

void QuoteSnapshotWriter::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void QuoteSnapshotWriter::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    fileWriter_.flushAll();
}

void QuoteSnapshotWriter::run() {
    while (running_) {
        std::this_thread::sleep_for(snapshotInterval_);
        if (!running_) break;
        takeSnapshot();
        reportUpdateRatesIfDue();
    }
}

void QuoteSnapshotWriter::takeSnapshot() {
    std::vector<QuoteSnapshotRow> rows;

    for (const auto& symbol : symbols_) {
        std::vector<std::string> venues;
        venues.reserve(symbol.nativeSymbols.size());
        for (const auto& [venue, nativeSymbol] : symbol.nativeSymbols) venues.push_back(venue);

        for (const auto& quote : store_.latestAcrossVenues(venues, symbol.symbolCode, quoteTtl_)) {
            rows.push_back(QuoteSnapshotRow{quote.exchangeName, quote.symbolCode, quote.bidPrice,
                                             quote.bidQty, quote.askPrice, quote.askQty,
                                             quote.exchangeTimestamp});
        }
    }

    if (rows.empty()) return;

    fileWriter_.recordAll(rows);
}

void QuoteSnapshotWriter::reportUpdateRatesIfDue() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - lastUpdateRateReport_;
    if (elapsed < kUpdateRateReportInterval) return;

    const double elapsedSeconds =
        std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
    const auto counts = store_.snapshotAndResetCounts();
    lastUpdateRateReport_ = now;
    if (counts.empty()) return;

    std::vector<QuoteUpdateStatRow> rows;
    rows.reserve(counts.size());
    for (const auto& count : counts) {
        rows.push_back(QuoteUpdateStatRow{count.exchangeName, count.symbolCode,
                                           count.count / elapsedSeconds});
    }

    try {
        repository_.upsertQuoteUpdateStats(rows);
    } catch (const std::exception& ex) {
        std::cerr << "QuoteSnapshotWriter: failed to persist update-rate stats: " << ex.what() << "\n";
    }
}
