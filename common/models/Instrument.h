#pragma once
#include <string>

// Exchange-specific listing of a Symbol. Maps to the "instrument" table.
struct Instrument {
    std::string exchangeName;  // "Binance" or "Bybit"
    std::string nativeSymbol;  // exchange's own symbol string, e.g. "BTCUSDT"
    std::string baseAsset;     // "BTC"
    std::string quoteAsset;    // "USDT"
    bool isActive = false;     // true if trading is currently enabled

    double tickSize = 0.0;     // minimum price increment
    double stepSize = 0.0;     // minimum quantity increment
    double minQty = 0.0;       // minimum order quantity
    double minNotional = 0.0;  // minimum order value
};
