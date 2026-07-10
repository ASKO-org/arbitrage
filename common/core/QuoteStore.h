#pragma once
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "marketdata/Quote.h"

// Thread-safe latest-quote-per-(venue,symbol) cache with TTL-aware lookup.
class QuoteStore {
public:
    void update(const Quote& quote);

    // Returns the latest quote for exchangeName/symbolCode, or nullopt if
    // absent or older than maxAge.
    std::optional<Quote> latest(const std::string& exchangeName, const std::string& symbolCode,
                                 std::chrono::milliseconds maxAge) const;

    // Returns the fresh quotes for symbolCode across every one of the given
    // venues, skipping venues with no quote or a stale one. Read as one
    // consistent snapshot rather than N separate lock acquisitions.
    std::vector<Quote> latestAcrossVenues(const std::vector<std::string>& venues,
                                           const std::string& symbolCode,
                                           std::chrono::milliseconds maxAge) const;

private:
    static std::string key(const std::string& exchangeName, const std::string& symbolCode);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Quote> quotes_;
};
