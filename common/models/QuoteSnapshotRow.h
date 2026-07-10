#pragma once
#include <chrono>
#include <string>

// One recorded best-bid/ask observation for a symbol on a venue, destined
// for the "quote_snapshot" table.
struct QuoteSnapshotRow {
    std::string exchangeName;
    std::string symbolCode;
    double bidPrice = 0.0;
    double bidQty = 0.0;
    double askPrice = 0.0;
    double askQty = 0.0;
    std::chrono::system_clock::time_point quoteTime;
};
