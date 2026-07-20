#pragma once
#include <chrono>
#include <string>

// One recorded best-bid/ask observation for a symbol on a venue, read from
// QuoteStore and appended to a per-exchange, per-day binary snapshot file
// by QuoteSnapshotFileWriter.
struct QuoteSnapshotRow {
    std::string exchangeName;
    std::string symbolCode;
    double bidPrice = 0.0;
    double bidQty = 0.0;
    double askPrice = 0.0;
    double askQty = 0.0;
    std::chrono::system_clock::time_point quoteTime;
};
