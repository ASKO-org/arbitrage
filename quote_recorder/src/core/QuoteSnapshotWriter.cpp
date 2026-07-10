#include "core/QuoteSnapshotWriter.h"

#include <iostream>

#include "models/QuoteSnapshotRow.h"

QuoteSnapshotWriter::QuoteSnapshotWriter(const QuoteStore& store, std::vector<TrackedSymbol> symbols,
                                          DatabaseRepository& repository,
                                          std::chrono::milliseconds quoteTtl,
                                          std::chrono::milliseconds snapshotInterval)
    : store_(store),
      symbols_(std::move(symbols)),
      repository_(repository),
      quoteTtl_(quoteTtl),
      snapshotInterval_(snapshotInterval) {}

QuoteSnapshotWriter::~QuoteSnapshotWriter() { stop(); }

void QuoteSnapshotWriter::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void QuoteSnapshotWriter::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void QuoteSnapshotWriter::run() {
    while (running_) {
        std::this_thread::sleep_for(snapshotInterval_);
        if (!running_) break;
        takeSnapshot();
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

    try {
        repository_.insertQuoteSnapshots(rows);
    } catch (const std::exception& ex) {
        std::cerr << "QuoteSnapshotWriter: failed to persist snapshot: " << ex.what() << "\n";
    }
}
