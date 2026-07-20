#pragma once
#include <string>

// Measured update rate for one (exchange, symbol), destined for the
// "quote_update_stats" table — a live gauge (one row per symbol, upserted
// in place), not a growing history.
struct QuoteUpdateStatRow {
    std::string exchangeName;
    std::string symbolCode;
    double updatesPerSecond = 0.0;
};
