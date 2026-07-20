#pragma once
#include <chrono>
#include <string>

#include <nlohmann/json.hpp>

// Best bid/ask snapshot for one symbol on one exchange.
struct Quote {
    std::string exchangeName;
    std::string symbolCode;  // canonical code, e.g. "BTCUSDT"
    double bidPrice = 0.0;
    double bidQty = 0.0;
    double askPrice = 0.0;
    double askQty = 0.0;
    std::chrono::system_clock::time_point exchangeTimestamp;
    std::chrono::steady_clock::time_point receivedAt;  // set by the connector on arrival; used for TTL checks
};

// receivedAt is steady_clock (monotonic, process-local) and deliberately not
// carried across the wire — a subscriber stamps its own receivedAt on
// arrival, same as a connector does today.
inline nlohmann::json toJson(const Quote& quote) {
    const auto exchangeTimestampUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                          quote.exchangeTimestamp.time_since_epoch())
                                          .count();
    return nlohmann::json{
        {"exchange_name", quote.exchangeName}, {"symbol_code", quote.symbolCode},
        {"bid_price", quote.bidPrice},          {"bid_qty", quote.bidQty},
        {"ask_price", quote.askPrice},          {"ask_qty", quote.askQty},
        {"exchange_timestamp_us", exchangeTimestampUs},
    };
}

// Best-effort exchange event time from an epoch-ms field a connector parsed
// out of its own payload. Falls back to local receipt time if the field was
// absent (epochMs <= 0) or wildly implausible — a value more than a few
// seconds off from local time is far more likely to be clock skew or a
// misparsed field than genuine wire latency, and reporting "unavailable" is
// safer than reporting a number that's confidently wrong.
inline std::chrono::system_clock::time_point exchangeTimestampOrNow(int64_t epochMs) {
    const auto now = std::chrono::system_clock::now();
    if (epochMs <= 0) return now;
    const auto parsed = std::chrono::system_clock::time_point(std::chrono::milliseconds(epochMs));
    const auto driftMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - parsed).count();
    if (driftMs < -5000 || driftMs > 30000) return now;
    return parsed;
}

inline Quote quoteFromJson(const nlohmann::json& json) {
    Quote quote;
    quote.exchangeName = json.at("exchange_name").get<std::string>();
    quote.symbolCode = json.at("symbol_code").get<std::string>();
    quote.bidPrice = json.at("bid_price").get<double>();
    quote.bidQty = json.at("bid_qty").get<double>();
    quote.askPrice = json.at("ask_price").get<double>();
    quote.askQty = json.at("ask_qty").get<double>();
    quote.exchangeTimestamp = std::chrono::system_clock::time_point(
        std::chrono::microseconds(json.value("exchange_timestamp_us", int64_t{0})));
    quote.receivedAt = std::chrono::steady_clock::now();
    return quote;
}
