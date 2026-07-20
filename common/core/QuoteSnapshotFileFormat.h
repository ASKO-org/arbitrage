#pragma once
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

// Shared on-disk format details for the quote-snapshot binary files written
// by QuoteSnapshotFileWriter (quote_recorder) and read back by
// QuoteSnapshotFileReader (common) — kept in one place so the two can never
// silently disagree on magic bytes, version, or the file naming convention.
inline constexpr char kQuoteSnapshotMagic[4] = {'Q', 'S', 'N', 'P'};
inline constexpr std::uint16_t kQuoteSnapshotFormatVersion = 1;

// UTC calendar day ("YYYYMMDD") that timePoint falls on.
inline std::string quoteSnapshotDayKey(std::chrono::system_clock::time_point timePoint) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(timePoint);
    std::tm utcTime{};
    gmtime_r(&seconds, &utcTime);

    std::ostringstream out;
    out << std::put_time(&utcTime, "%Y%m%d");
    return out.str();
}

// Path of the snapshot file for exchangeName on the given "YYYYMMDD" day.
inline std::string quoteSnapshotFilePath(const std::string& dir, const std::string& exchangeName,
                                         const std::string& day) {
    return (std::filesystem::path(dir) / (exchangeName + "_" + day + ".bin")).string();
}
