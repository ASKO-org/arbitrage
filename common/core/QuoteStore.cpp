#include "core/QuoteStore.h"

std::string QuoteStore::key(const std::string& exchangeName, const std::string& symbolCode) {
    return exchangeName + "|" + symbolCode;
}

void QuoteStore::update(const Quote& quote) {
    std::lock_guard<std::mutex> lock(mutex_);
    quotes_[key(quote.exchangeName, quote.symbolCode)] = quote;
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
