#include "database/DatabaseRepository.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include <pqxx/pqxx>

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

    // Live gauge of measured update rate per (exchange, symbol) — one row
    // each, overwritten in place, not a growing history. See
    // QuoteStore::snapshotAndResetCounts() for where the counts come from.
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS quote_update_stats (
            exchange_name VARCHAR(32) NOT NULL,
            symbol_code VARCHAR(64) NOT NULL,
            updates_per_sec DOUBLE PRECISION NOT NULL,
            measured_at TIMESTAMPTZ NOT NULL DEFAULT now(),
            PRIMARY KEY (exchange_name, symbol_code)
        )
    )");

    // System of record for execution_service, beyond whatever's still
    // sitting in the execution:orders/execution:reports Redis streams.
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS orders (
            order_id VARCHAR(64) PRIMARY KEY,
            strategy_id VARCHAR(64) NOT NULL DEFAULT '',
            venue VARCHAR(32) NOT NULL,
            symbol VARCHAR(64) NOT NULL,
            side VARCHAR(8) NOT NULL,
            type VARCHAR(8) NOT NULL,
            qty DOUBLE PRECISION NOT NULL,
            price DOUBLE PRECISION NOT NULL DEFAULT 0,
            status VARCHAR(16) NOT NULL,
            created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
            updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS fills (
            id SERIAL PRIMARY KEY,
            order_id VARCHAR(64) NOT NULL REFERENCES orders(order_id),
            qty DOUBLE PRECISION NOT NULL,
            price DOUBLE PRECISION NOT NULL,
            fee DOUBLE PRECISION NOT NULL DEFAULT 0,
            executed_at TIMESTAMPTZ NOT NULL DEFAULT now()
        )
    )");

    txn.commit();
}

void DatabaseRepository::saveInstruments(const std::vector<Instrument>& instruments) {
    pqxx::work txn(*connection_);

    for (const auto& instrument : instruments) {
        // base/quote must be case-normalized here, in the one shared
        // insertion point — otherwise an exchange returning lowercase codes
        // (e.g. HTX's "btc"/"usdt") both derives a mismatched canonical code
        // AND creates an entirely separate symbol row from every other
        // exchange's "BTC"/"USDT", since the (base_asset, quote_asset)
        // uniqueness check is case-sensitive.
        std::string baseAsset = instrument.baseAsset;
        std::string quoteAsset = instrument.quoteAsset;
        std::transform(baseAsset.begin(), baseAsset.end(), baseAsset.begin(),
                        [](unsigned char c) { return std::toupper(c); });
        std::transform(quoteAsset.begin(), quoteAsset.end(), quoteAsset.begin(),
                        [](unsigned char c) { return std::toupper(c); });

        // code must be derived from base/quote, not any one exchange's
        // nativeSymbol — otherwise whichever exchange last upserts a given
        // base/quote pair silently overwrites the canonical code for every
        // other exchange (e.g. OKX's "BTC-USDT-SWAP" clobbering Binance's
        // "BTCUSDT"), which would corrupt symbol_code continuity for any
        // historical data keyed on it.
        const std::string code = baseAsset + quoteAsset;

        const auto symbolRow = txn.exec_params1(
            R"(
                INSERT INTO symbol (code, base_asset, quote_asset)
                VALUES ($1, $2, $3)
                ON CONFLICT (base_asset, quote_asset)
                DO UPDATE SET code = EXCLUDED.code
                RETURNING id
            )",
            code, baseAsset, quoteAsset);
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

void DatabaseRepository::upsertQuoteUpdateStats(const std::vector<QuoteUpdateStatRow>& rows) {
    if (rows.empty()) return;

    pqxx::work txn(*connection_);
    for (const auto& row : rows) {
        txn.exec_params(
            R"(
                INSERT INTO quote_update_stats (exchange_name, symbol_code, updates_per_sec, measured_at)
                VALUES ($1, $2, $3, now())
                ON CONFLICT (exchange_name, symbol_code)
                DO UPDATE SET updates_per_sec = EXCLUDED.updates_per_sec, measured_at = EXCLUDED.measured_at
            )",
            row.exchangeName, row.symbolCode, row.updatesPerSecond);
    }
    txn.commit();
}

void DatabaseRepository::upsertOrder(const Order& order, OrderStatus status) {
    pqxx::work txn(*connection_);
    txn.exec_params(
        R"(
            INSERT INTO orders (order_id, strategy_id, venue, symbol, side, type, qty, price, status, created_at, updated_at)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, now(), now())
            ON CONFLICT (order_id) DO UPDATE SET
                status = EXCLUDED.status,
                updated_at = now()
        )",
        order.orderId, order.strategyId, order.venue, order.symbol, toString(order.side),
        toString(order.type), order.qty, order.price, toString(status));
    txn.commit();
}

std::optional<OrderStatus> DatabaseRepository::findOrderStatus(const std::string& orderId) const {
    pqxx::work txn(*connection_);
    const auto rows = txn.exec_params("SELECT status FROM orders WHERE order_id = $1", orderId);
    txn.commit();
    if (rows.empty()) return std::nullopt;
    return orderStatusFromString(rows[0][0].as<std::string>());
}

void DatabaseRepository::insertFill(const Fill& fill) {
    pqxx::work txn(*connection_);
    txn.exec_params(
        R"(
            INSERT INTO fills (order_id, qty, price, fee, executed_at)
            VALUES ($1, $2, $3, $4, now())
        )",
        fill.orderId, fill.qty, fill.price, fill.fee);
    txn.commit();
}

double DatabaseRepository::filledQtyForOrder(const std::string& orderId) const {
    pqxx::work txn(*connection_);
    const auto row = txn.exec_params1("SELECT COALESCE(SUM(qty), 0) FROM fills WHERE order_id = $1", orderId);
    txn.commit();
    return row[0].as<double>();
}

std::vector<std::pair<Order, OrderStatus>> DatabaseRepository::loadOpenOrders() const {
    pqxx::work txn(*connection_);

    const auto rows = txn.exec(R"(
        SELECT order_id, strategy_id, venue, symbol, side, type, qty, price, status
        FROM orders
        WHERE status NOT IN ('filled', 'rejected', 'cancelled')
    )");

    std::vector<std::pair<Order, OrderStatus>> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        Order order;
        order.orderId = row[0].as<std::string>();
        order.strategyId = row[1].as<std::string>();
        order.venue = row[2].as<std::string>();
        order.symbol = row[3].as<std::string>();
        order.side = orderSideFromString(row[4].as<std::string>());
        order.type = orderTypeFromString(row[5].as<std::string>());
        order.qty = row[6].as<double>();
        order.price = row[7].as<double>();
        result.emplace_back(std::move(order), orderStatusFromString(row[8].as<std::string>()));
    }

    txn.commit();
    return result;
}

double DatabaseRepository::netPosition(const std::string& venue, const std::string& symbol) const {
    pqxx::work txn(*connection_);
    const auto row = txn.exec_params1(
        R"(
            SELECT COALESCE(SUM(CASE WHEN o.side = 'buy' THEN f.qty ELSE -f.qty END), 0)
            FROM fills f
            JOIN orders o ON o.order_id = f.order_id
            WHERE o.venue = $1 AND o.symbol = $2
        )",
        venue, symbol);
    txn.commit();
    return row[0].as<double>();
}
