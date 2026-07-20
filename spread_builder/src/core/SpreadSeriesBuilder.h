#pragma once
#include <chrono>
#include <string>
#include <vector>

// One pair of computed spreads at a fixed-interval bucket in time, between
// two fixed exchanges:
//   entry: buy on exchange1 (its ask), sell on exchange2 (its bid)
//          -> (bid2/ask1 - 1) x 100
//   exit:  buy on exchange2 (its ask), sell on exchange1 (its bid) — the
//          reverse trade that unwinds an entry position
//          -> (ask2/bid1 - 1) x 100
// Both are raw/gross, percent-scaled ratios — no fee or slippage buffer.
// The *SpreadBps field names predate the switch from bps to percent scaling
// and are kept as-is to avoid a breaking rename for existing consumers.
struct SpreadPoint {
    std::chrono::system_clock::time_point time;
    double entrySpreadBps = 0.0;
    double entryBuyPrice = 0.0;   // exchange1 ask
    double entrySellPrice = 0.0;  // exchange2 bid
    double exitSpreadBps = 0.0;
    double exitBuyPrice = 0.0;    // exchange2 ask
    double exitSellPrice = 0.0;   // exchange1 bid
};

// Builds an entry/exit spread time series for one symbol between exactly
// two exchanges, reading their recorded history via QuoteSnapshotFileReader
// and forward-filling each onto a fixed-interval grid.
//
// Known simplification: forward-fill only starts once an exchange's first
// in-range record has been read, so the first few buckets of a query range
// may have no point yet if one exchange hasn't reported within [from, to].
class SpreadSeriesBuilder {
public:
    explicit SpreadSeriesBuilder(std::string snapshotDir);

    std::vector<SpreadPoint> build(const std::string& symbolCode, const std::string& exchange1,
                                    const std::string& exchange2,
                                    std::chrono::system_clock::time_point from,
                                    std::chrono::system_clock::time_point to,
                                    std::chrono::milliseconds interval) const;

private:
    std::string snapshotDir_;
};
