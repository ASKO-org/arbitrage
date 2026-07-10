#include "database/DatabaseRepository.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <unordered_map>

#include <pqxx/pqxx>

namespace {
// pqxx doesn't have a documented write-path conversion for
// std::chrono::system_clock::time_point, so quote_time is passed as an
// ISO-8601 string and left for Postgres to parse into timestamptz.
std::string toIso8601(std::chrono::system_clock::time_point timePoint) {
    const auto microsecondsSinceEpoch =
        std::chrono::duration_cast<std::chrono::microseconds>(timePoint.time_since_epoch());
    const std::time_t seconds =
        std::chrono::duration_cast<std::chrono::seconds>(microsecondsSinceEpoch).count();
    const long microsecondsRemainder = microsecondsSinceEpoch.count() % 1000000;

    std::tm utcTime{};
    gmtime_r(&seconds, &utcTime);

    std::ostringstream out;
    out << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(6)
        << microsecondsRemainder << 'Z';
    return out.str();
}
}  // namespace

DatabaseRepository::DatabaseRepository(const std::string& connectionString)
    : connection_(std::make_unique<pqxx::connection>(connectionString)) {}

DatabaseRepository::~DatabaseRepository() = default;

void DatabaseRepository::ensureSchema() {
    pqxx::work txn(*connection_);

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS symbol (
            id SERIAL PRIMARY KEY,
            code VARCHAR(64) NOT NULL,
            base_asset VARCHAR(32) NOT NULL,
            quote_asset VARCHAR(32) NOT NULL,
            UNIQUE (base_asset, quote_asset)
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS instrument (
            id SERIAL PRIMARY KEY,
            symbol_id INTEGER NOT NULL REFERENCES symbol(id),
            exchange_name VARCHAR(32) NOT NULL,
            native_symbol VARCHAR(64) NOT NULL,
            is_active BOOLEAN NOT NULL DEFAULT false,
            tick_size DOUBLE PRECISION,
            step_size DOUBLE PRECISION,
            min_qty DOUBLE PRECISION,
            min_notional DOUBLE PRECISION,
            updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
            UNIQUE (exchange_name, native_symbol)
        )
    )");

    // Operator-managed: which (venue, native symbol) pairs quote_recorder
    // should record. Edited directly via psql.
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS recorder_watchlist (
            exchange_name VARCHAR(32) NOT NULL,
            native_symbol VARCHAR(64) NOT NULL,
            PRIMARY KEY (exchange_name, native_symbol),
            FOREIGN KEY (exchange_name, native_symbol) REFERENCES instrument(exchange_name, native_symbol)
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS quote_snapshot (
            id BIGSERIAL PRIMARY KEY,
            exchange_name VARCHAR(32) NOT NULL,
            symbol_code VARCHAR(64) NOT NULL,
            bid_price DOUBLE PRECISION NOT NULL,
            bid_qty DOUBLE PRECISION NOT NULL,
            ask_price DOUBLE PRECISION NOT NULL,
            ask_qty DOUBLE PRECISION NOT NULL,
            quote_time TIMESTAMPTZ NOT NULL,
            recorded_at TIMESTAMPTZ NOT NULL DEFAULT now()
        )
    )");
    txn.exec(R"(
        CREATE INDEX IF NOT EXISTS quote_snapshot_lookup
            ON quote_snapshot (exchange_name, symbol_code, recorded_at)
    )");

    txn.commit();
}

void DatabaseRepository::saveInstruments(const std::vector<Instrument>& instruments) {
    pqxx::work txn(*connection_);

    for (const auto& instrument : instruments) {
        // code must be derived from base/quote, not any one exchange's
        // nativeSymbol — otherwise whichever exchange last upserts a given
        // base/quote pair silently overwrites the canonical code for every
        // other exchange (e.g. OKX's "BTC-USDT-SWAP" clobbering Binance's
        // "BTCUSDT"), which would corrupt symbol_code continuity for any
        // historical data keyed on it.
        const std::string code = instrument.baseAsset + instrument.quoteAsset;

        const auto symbolRow = txn.exec_params1(
            R"(
                INSERT INTO symbol (code, base_asset, quote_asset)
                VALUES ($1, $2, $3)
                ON CONFLICT (base_asset, quote_asset)
                DO UPDATE SET code = EXCLUDED.code
                RETURNING id
            )",
            code, instrument.baseAsset, instrument.quoteAsset);
        const int symbolId = symbolRow[0].as<int>();

        txn.exec_params(
            R"(
                INSERT INTO instrument
                    (symbol_id, exchange_name, native_symbol, is_active,
                     tick_size, step_size, min_qty, min_notional, updated_at)
                VALUES ($1, $2, $3, $4, $5, $6, $7, $8, now())
                ON CONFLICT (exchange_name, native_symbol)
                DO UPDATE SET
                    symbol_id = EXCLUDED.symbol_id,
                    is_active = EXCLUDED.is_active,
                    tick_size = EXCLUDED.tick_size,
                    step_size = EXCLUDED.step_size,
                    min_qty = EXCLUDED.min_qty,
                    min_notional = EXCLUDED.min_notional,
                    updated_at = now()
            )",
            symbolId, instrument.exchangeName, instrument.nativeSymbol, instrument.isActive,
            instrument.tickSize, instrument.stepSize, instrument.minQty, instrument.minNotional);
    }

    txn.commit();
}

std::vector<TrackedSymbol> DatabaseRepository::loadSymbolsActiveOnVenues(
    const std::vector<std::string>& venues) const {
    pqxx::work txn(*connection_);

    pqxx::params params;
    std::string placeholders;
    for (size_t i = 0; i < venues.size(); ++i) {
        if (i) placeholders += ", ";
        placeholders += "$" + std::to_string(i + 1);
        params.append(venues[i]);
    }

    const auto rows = txn.exec(
        "SELECT s.code, i.exchange_name, i.native_symbol "
        "FROM symbol s "
        "JOIN instrument i ON i.symbol_id = s.id "
        "WHERE i.exchange_name IN (" +
            placeholders + ") AND i.is_active",
        params);

    std::unordered_map<std::string, TrackedSymbol> bySymbolCode;
    for (const auto& row : rows) {
        const auto symbolCode = row[0].as<std::string>();
        auto& tracked = bySymbolCode[symbolCode];
        tracked.symbolCode = symbolCode;
        tracked.nativeSymbols[row[1].as<std::string>()] = row[2].as<std::string>();
    }

    std::vector<TrackedSymbol> result;
    for (auto& [code, tracked] : bySymbolCode) {
        if (tracked.nativeSymbols.size() == venues.size()) {
            result.push_back(std::move(tracked));
        }
    }

    txn.commit();
    return result;
}

std::vector<TrackedSymbol> DatabaseRepository::loadWatchlistSymbols() const {
    pqxx::work txn(*connection_);

    const auto rows = txn.exec(
        "SELECT s.code, i.exchange_name, i.native_symbol "
        "FROM recorder_watchlist w "
        "JOIN instrument i ON i.exchange_name = w.exchange_name AND i.native_symbol = w.native_symbol "
        "JOIN symbol s ON s.id = i.symbol_id");

    std::unordered_map<std::string, TrackedSymbol> bySymbolCode;
    for (const auto& row : rows) {
        const auto symbolCode = row[0].as<std::string>();
        auto& tracked = bySymbolCode[symbolCode];
        tracked.symbolCode = symbolCode;
        tracked.nativeSymbols[row[1].as<std::string>()] = row[2].as<std::string>();
    }

    std::vector<TrackedSymbol> result;
    result.reserve(bySymbolCode.size());
    for (auto& [code, tracked] : bySymbolCode) {
        result.push_back(std::move(tracked));
    }

    txn.commit();
    return result;
}

void DatabaseRepository::insertQuoteSnapshots(const std::vector<QuoteSnapshotRow>& rows) {
    if (rows.empty()) return;

    pqxx::work txn(*connection_);
    auto stream = pqxx::stream_to::table(
        txn, {"quote_snapshot"},
        {"exchange_name", "symbol_code", "bid_price", "bid_qty", "ask_price", "ask_qty", "quote_time"});

    for (const auto& row : rows) {
        stream << std::make_tuple(row.exchangeName, row.symbolCode, row.bidPrice, row.bidQty,
                                   row.askPrice, row.askQty, toIso8601(row.quoteTime));
    }

    stream.complete();
    txn.commit();
}
