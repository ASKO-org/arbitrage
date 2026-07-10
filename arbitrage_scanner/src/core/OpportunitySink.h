#pragma once
#include <chrono>
#include <iostream>

#include <nlohmann/json.hpp>

#include "core/Opportunity.h"

// Destination for detected arbitrage opportunities.
class OpportunitySink {
public:
    virtual ~OpportunitySink() = default;
    virtual void record(const ArbitrageOpportunity& opportunity) = 0;
};

inline nlohmann::json toJson(const ArbitrageOpportunity& opportunity) {
    const auto detectedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   opportunity.detectedAt.time_since_epoch())
                                   .count();
    return nlohmann::json{
        {"symbol", opportunity.symbolCode},
        {"buy_exchange", opportunity.buyExchange},
        {"buy_price", opportunity.buyPrice},
        {"sell_exchange", opportunity.sellExchange},
        {"sell_price", opportunity.sellPrice},
        {"gross_spread_bps", opportunity.grossSpreadBps},
        {"net_spread_bps", opportunity.netSpreadBps},
        {"detected_at_ms", detectedAtMs},
    };
}

// Writes each opportunity as one JSON object per line to stdout.
class StdoutOpportunitySink : public OpportunitySink {
public:
    void record(const ArbitrageOpportunity& opportunity) override {
        std::cout << toJson(opportunity).dump() << std::endl;
    }
};
