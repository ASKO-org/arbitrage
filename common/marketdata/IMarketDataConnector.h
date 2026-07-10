#pragma once
#include <functional>
#include <string>
#include <vector>

#include "marketdata/Quote.h"
#include "models/TrackedSymbol.h"

// Base interface every venue's streaming market-data connector implements.
// One venue = one (exchange, asset type) pair, e.g. "BINANCE_Spot" and
// "BINANCE_Fut" are two separate connectors, not modes of one connector.
class IMarketDataConnector {
public:
    virtual ~IMarketDataConnector() = default;

    virtual std::string exchangeName() const = 0;

    // Must be called once, before start().
    virtual void subscribe(const std::vector<TrackedSymbol>& symbols) = 0;

    // Invoked on this connector's own IO thread for every quote update.
    virtual void setOnQuote(std::function<void(const Quote&)> onQuote) = 0;

    // Opens the WebSocket connection. Non-blocking; runs on an internal IO thread.
    virtual void start() = 0;

    // Closes the connection and joins internal threads.
    virtual void stop() = 0;
};
