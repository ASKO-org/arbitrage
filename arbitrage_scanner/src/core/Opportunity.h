#pragma once
#include <chrono>
#include <string>

// A detected cross-exchange arbitrage edge: buy on one exchange, sell on the other.
struct ArbitrageOpportunity {
    std::string symbolCode;
    std::string buyExchange;
    double buyPrice = 0.0;   // ask on the buy side
    std::string sellExchange;
    double sellPrice = 0.0;  // bid on the sell side
    double grossSpreadBps = 0.0;
    double netSpreadBps = 0.0;  // gross minus fee/slippage buffers
    std::chrono::system_clock::time_point detectedAt;
};
