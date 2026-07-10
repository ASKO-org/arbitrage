#pragma once
#include <memory>
#include <string>
#include <vector>

#include "models/Instrument.h"
#include "models/QuoteSnapshotRow.h"
#include "models/TrackedSymbol.h"

namespace pqxx {
class connection;
}

// Persists instruments into PostgreSQL, normalizing them into the
// "symbol" (exchange-agnostic pair) and "instrument" (per-exchange listing) tables.
class DatabaseRepository {
public:
    explicit DatabaseRepository(const std::string& connectionString);
    ~DatabaseRepository();

    // Creates the symbol/instrument tables if they don't already exist.
    void ensureSchema();

    // Upserts every instrument, creating its parent symbol row as needed.
    // Runs in a single transaction; throws std::runtime_error on failure.
    void saveInstruments(const std::vector<Instrument>& instruments);

    // Returns every symbol that is actively tradable on every one of the given
    // venues, with each venue's native symbol string attached.
    std::vector<TrackedSymbol> loadSymbolsActiveOnVenues(const std::vector<std::string>& venues) const;

    // Returns every symbol referenced by recorder_watchlist, with each
    // watchlisted venue's native symbol attached. Unlike
    // loadSymbolsActiveOnVenues, a symbol need not appear on every venue —
    // each watchlist row independently contributes to its symbol's entry.
    std::vector<TrackedSymbol> loadWatchlistSymbols() const;

    // Bulk-inserts recorded quote snapshots in one transaction via COPY.
    // Throws std::runtime_error on failure. No-op if rows is empty.
    void insertQuoteSnapshots(const std::vector<QuoteSnapshotRow>& rows);

private:
    std::unique_ptr<pqxx::connection> connection_;
};
