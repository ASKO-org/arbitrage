#pragma once
#include <memory>
#include <utility>
#include <vector>

#include "core/OpportunitySink.h"

// Fans a single opportunity out to multiple sinks.
class CompositeOpportunitySink : public OpportunitySink {
public:
    explicit CompositeOpportunitySink(std::vector<std::shared_ptr<OpportunitySink>> sinks)
        : sinks_(std::move(sinks)) {}

    void record(const ArbitrageOpportunity& opportunity) override {
        for (const auto& sink : sinks_) {
            sink->record(opportunity);
        }
    }

private:
    std::vector<std::shared_ptr<OpportunitySink>> sinks_;
};
