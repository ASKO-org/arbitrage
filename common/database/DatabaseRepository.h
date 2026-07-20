#pragma once
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "models/ExecutionReport.h"
#include "models/Fill.h"
#include "models/Instrument.h"
#include "models/Order.h"
#include "models/QuoteUpdateStatRow.h"
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

    // Upserts measured update rates into quote_update_stats — one row per
    // (exchange, symbol), overwritten in place each call. No-op if rows is
    // empty.
    void upsertQuoteUpdateStats(const std::vector<QuoteUpdateStatRow>& rows);

    // Inserts a new order row, or updates status/updated_at if order_id
    // already exists (a redelivered stream entry re-reports the same order).
    void upsertOrder(const Order& order, OrderStatus status);

    // The recorded status for order_id, or nullopt if never recorded. The
    // idempotency check OrderRouter uses on a redelivered stream entry: a
    // terminal status (filled/rejected/cancelled) means skip it outright; a
    // non-terminal one ("acked") means a prior attempt may not have reached
    // the exchange, so it's still safe (and necessary) to retry placeOrder —
    // the exchange itself dedupes on order.orderId as the client order id.
    std::optional<OrderStatus> findOrderStatus(const std::string& orderId) const;

    // Appends one fill against an already-recorded order.
    void insertFill(const Fill& fill);

    // Sum of qty already recorded in fills for order_id. Startup
    // reconciliation uses this to insert only the delta between what the
    // exchange now reports as cumulative filled quantity and what's already
    // stored, so re-reconciling (or a fill recorded before a crash) doesn't
    // double-count.
    double filledQtyForOrder(const std::string& orderId) const;

    // Every order not yet in a terminal state (filled/rejected/cancelled),
    // for execution_service's startup reconciliation against live exchange
    // state.
    std::vector<std::pair<Order, OrderStatus>> loadOpenOrders() const;

    // Net signed position (buys positive, sells negative) accumulated from
    // fills for one venue+symbol — the input to OrderRouter's aggregate
    // exposure check across strategies sharing the same account.
    double netPosition(const std::string& venue, const std::string& symbol) const;

private:
    std::unique_ptr<pqxx::connection> connection_;
};
