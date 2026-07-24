#pragma once
#include <string>
#include <vector>

#include "models/Order.h"

// Outcome of a placeOrder() call — synchronous, since every connector's REST
// order-placement endpoint returns either an immediate fill/reject or an
// acknowledged-but-not-yet-filled state in its own response.
struct OrderResult {
    bool accepted = false;         // false if the exchange rejected the order outright
    double filledQty = 0.0;        // >0 if the exchange filled some/all of it immediately
    double avgPrice = 0.0;
    bool fullyFilled = false;
    std::string errorMessage;      // set when accepted == false
};

// One asset's balance on a venue's spot account.
struct AssetBalance {
    std::string asset;
    double free = 0.0;    // available to trade/withdraw
    double locked = 0.0;  // held in open orders
};

// One open leveraged (futures/derivatives) position — a separate account
// facility from spot balances above, even on the same API key/account.
struct Position {
    std::string symbol;
    std::string side;  // "long" or "short"
    double size = 0.0;
    double entryPrice = 0.0;
    double markPrice = 0.0;
    double unrealizedPnl = 0.0;
    double leverage = 0.0;
};

// Base interface every venue's trading connector implements. One venue = one
// (exchange, asset type) pair, mirroring IMarketDataConnector's shape.
class IExecutionConnector {
public:
    virtual ~IExecutionConnector() = default;

    virtual std::string exchangeName() const = 0;

    // Places the order. order.orderId is passed through as the exchange's
    // client-order-id field so the venue itself de-dupes retried submissions.
    // Throws std::runtime_error only on transport/auth failure — an exchange
    // rejection (bad price, insufficient balance, etc.) comes back as
    // OrderResult{accepted = false}, not an exception.
    virtual OrderResult placeOrder(const Order& order) = 0;

    // Cancels a resting order by the client order id it was placed with.
    // Throws std::runtime_error on transport/auth failure.
    virtual void cancelOrder(const std::string& orderId, const std::string& symbol) = 0;

    // Queries the exchange for an order's current state by the client order
    // id it was placed with. accepted == true means the exchange found the
    // order (regardless of its fill state, in filledQty/avgPrice/fullyFilled);
    // accepted == false means it wasn't found or the query failed
    // (errorMessage set). Used for execution_service's startup reconciliation
    // — resolving an order left non-terminal by a crash without trusting a
    // client-side guess.
    virtual OrderResult getOrderStatus(const std::string& orderId, const std::string& symbol) = 0;

    // Current spot account balances, one entry per asset with a nonzero
    // free or locked amount. Throws std::runtime_error on transport/auth
    // failure.
    virtual std::vector<AssetBalance> getBalances() = 0;

    // Currently open leveraged (futures/derivatives) positions on this
    // account — independent of this connector's own order-placement
    // category, since Bybit/Bitget both let one account hold spot balances
    // and derivatives positions side by side. Throws std::runtime_error on
    // transport/auth failure.
    virtual std::vector<Position> getPositions() = 0;
};
