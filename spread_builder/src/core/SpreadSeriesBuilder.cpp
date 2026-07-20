#include "core/SpreadSeriesBuilder.h"

#include "core/QuoteSnapshotFileReader.h"
#include "models/QuoteSnapshotRow.h"

namespace {
struct Cursor {
    std::vector<QuoteSnapshotRow> rows;
    std::size_t index = 0;
    const QuoteSnapshotRow* current = nullptr;

    void advanceTo(std::chrono::system_clock::time_point bucketTime) {
        while (index < rows.size() && rows[index].quoteTime <= bucketTime) {
            current = &rows[index];
            ++index;
        }
    }
};
}  // namespace

SpreadSeriesBuilder::SpreadSeriesBuilder(std::string snapshotDir)
    : snapshotDir_(std::move(snapshotDir)) {}

std::vector<SpreadPoint> SpreadSeriesBuilder::build(const std::string& symbolCode,
                                                     const std::string& exchange1,
                                                     const std::string& exchange2,
                                                     std::chrono::system_clock::time_point from,
                                                     std::chrono::system_clock::time_point to,
                                                     std::chrono::milliseconds interval) const {
    std::vector<SpreadPoint> points;
    if (from > to || interval.count() <= 0) return points;

    const QuoteSnapshotFileReader reader(snapshotDir_);
    Cursor cursor1{reader.read(exchange1, symbolCode, from, to)};
    Cursor cursor2{reader.read(exchange2, symbolCode, from, to)};
    if (cursor1.rows.empty() || cursor2.rows.empty()) return points;

    for (auto bucketTime = from; bucketTime <= to; bucketTime += interval) {
        cursor1.advanceTo(bucketTime);
        cursor2.advanceTo(bucketTime);
        if (cursor1.current == nullptr || cursor2.current == nullptr) continue;

        const auto& quote1 = *cursor1.current;
        const auto& quote2 = *cursor2.current;
        if (quote1.askPrice <= 0.0 || quote1.bidPrice <= 0.0 || quote2.askPrice <= 0.0 ||
            quote2.bidPrice <= 0.0) {
            continue;
        }

        // entry: buy exchange1 @ ask1, sell exchange2 @ bid2 -> (bid2/ask1 - 1) x 100
        // exit: buy exchange2 @ ask2, sell exchange1 @ bid1 -> (ask2/bid1 - 1) x 100
        SpreadPoint point;
        point.time = bucketTime;
        point.entryBuyPrice = quote1.askPrice;
        point.entrySellPrice = quote2.bidPrice;
        point.entrySpreadBps = (quote2.bidPrice / quote1.askPrice - 1.0) * 100.0;
        point.exitBuyPrice = quote2.askPrice;
        point.exitSellPrice = quote1.bidPrice;
        point.exitSpreadBps = (quote2.askPrice / quote1.bidPrice - 1.0) * 100.0;

        points.push_back(point);
    }

    return points;
}
