#pragma once
#include <chrono>
#include <string>

// Best bid/ask snapshot for one symbol on one exchange.
struct Quote {
    std::string exchangeName;
    std::string symbolCode;  // canonical code, e.g. "BTCUSDT"
    double bidPrice = 0.0;
    double bidQty = 0.0;
    double askPrice = 0.0;
    double askQty = 0.0;
    std::chrono::system_clock::time_point exchangeTimestamp;
    std::chrono::steady_clock::time_point receivedAt;  // set by the connector on arrival; used for TTL checks
};
