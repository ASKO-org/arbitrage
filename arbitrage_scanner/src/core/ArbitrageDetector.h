#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/OpportunitySink.h"
#include "core/QuoteStore.h"
#include "models/TrackedSymbol.h"

// Periodically evaluates the best bid/ask across every tracked venue for
// every tracked symbol, and reports opportunities that clear the configured
// fee/slippage buffer. Adding a venue just means adding it to the venue
// list — this scans once per symbol per pass rather than comparing every
// pair of venues, so it doesn't get slower per-pair as venues grow.
class ArbitrageDetector {
public:
    ArbitrageDetector(const QuoteStore& store, std::vector<TrackedSymbol> symbols,
                       std::vector<std::string> venues, std::shared_ptr<OpportunitySink> sink,
                       std::chrono::milliseconds quoteTtl, std::chrono::milliseconds detectionInterval,
                       double feeBufferBps, double slippageBufferBps, double minNetSpreadBps);
    ~ArbitrageDetector();

    void start();
    void stop();

private:
    void run();
    void evaluateSymbol(const TrackedSymbol& symbol);

    const QuoteStore& store_;
    std::vector<TrackedSymbol> symbols_;
    std::vector<std::string> venues_;
    std::shared_ptr<OpportunitySink> sink_;
    std::chrono::milliseconds quoteTtl_;
    std::chrono::milliseconds detectionInterval_;
    double feeBufferBps_;
    double slippageBufferBps_;
    double minNetSpreadBps_;

    std::atomic<bool> running_{false};
    std::thread thread_;
};
