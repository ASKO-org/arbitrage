#include "core/QuoteStore.h"

std::string QuoteStore::key(const std::string& exchangeName, const std::string& symbolCode) {
    return exchangeName + "|" + symbolCode;
}

void QuoteStore::update(const Quote& quote) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string quoteKey = key(quote.exchangeName, quote.symbolCode);
    quotes_[quoteKey] = quote;

    auto& entry = updateCounts_[quoteKey];
    entry.exchangeName = quote.exchangeName;
    entry.symbolCode = quote.symbolCode;
    ++entry.count;
}

std::vector<QuoteStore::UpdateCount> QuoteStore::snapshotAndResetCounts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UpdateCount> result;
    result.reserve(updateCounts_.size());
    for (auto& [quoteKey, entry] : updateCounts_) {
        result.push_back(std::move(entry));
    }
    updateCounts_.clear();
    return result;
}

std::optional<Quote> QuoteStore::latest(const std::string& exchangeName, const std::string& symbolCode,
                                         std::chrono::milliseconds maxAge) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = quotes_.find(key(exchangeName, symbolCode));
    if (it == quotes_.end()) return std::nullopt;
    if (std::chrono::steady_clock::now() - it->second.receivedAt > maxAge) return std::nullopt;
    return it->second;
}

std::vector<Quote> QuoteStore::latestAcrossVenues(const std::vector<std::string>& venues,
                                                    const std::string& symbolCode,
                                                    std::chrono::milliseconds maxAge) const {
    std::vector<Quote> result;
    result.reserve(venues.size());

    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    for (const auto& venue : venues) {
        const auto it = quotes_.find(key(venue, symbolCode));
        if (it == quotes_.end()) continue;
        if (now - it->second.receivedAt > maxAge) continue;
        result.push_back(it->second);
    }
    return result;
}
