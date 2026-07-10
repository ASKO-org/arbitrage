#pragma once
#include <string>
#include <unordered_map>

// A canonical symbol and its native symbol string on every venue where it's
// actively tradable. Callers look up nativeSymbols.at(venueName) to translate
// between the canonical code and what a specific venue calls it.
struct TrackedSymbol {
    std::string symbolCode;  // canonical code, e.g. "BTCUSDT"
    std::unordered_map<std::string, std::string> nativeSymbols;  // venue name -> native symbol
};
