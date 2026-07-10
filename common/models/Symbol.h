#pragma once
#include <string>

// Exchange-agnostic trading pair, e.g. BTC/USDT. Maps to the "symbol" table.
struct Symbol {
    std::string code;        // canonical code, e.g. "BTCUSDT"
    std::string baseAsset;   // "BTC"
    std::string quoteAsset;  // "USDT"
};
