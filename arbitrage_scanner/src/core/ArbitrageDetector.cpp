#include "core/ArbitrageDetector.h"

#include <algorithm>

ArbitrageDetector::ArbitrageDetector(const QuoteStore& store, std::vector<TrackedSymbol> symbols,
                                      std::vector<std::string> venues,
                                      std::shared_ptr<OpportunitySink> sink,
                                      std::chrono::milliseconds quoteTtl,
                                      std::chrono::milliseconds detectionInterval,
                                      double feeBufferBps, double slippageBufferBps,
                                      double minNetSpreadBps)
    : store_(store),
      symbols_(std::move(symbols)),
      venues_(std::move(venues)),
      sink_(std::move(sink)),
      quoteTtl_(quoteTtl),
      detectionInterval_(detectionInterval),
      feeBufferBps_(feeBufferBps),
      slippageBufferBps_(slippageBufferBps),
      minNetSpreadBps_(minNetSpreadBps) {}

ArbitrageDetector::~ArbitrageDetector() { stop(); }

void ArbitrageDetector::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void ArbitrageDetector::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void ArbitrageDetector::run() {
    while (running_) {
        std::this_thread::sleep_for(detectionInterval_);
        if (!running_) break;
        for (const auto& symbol : symbols_) {
            evaluateSymbol(symbol);
        }
    }
}

void ArbitrageDetector::evaluateSymbol(const TrackedSymbol& symbol) {
    const auto quotes = store_.latestAcrossVenues(venues_, symbol.symbolCode, quoteTtl_);
    if (quotes.size() < 2) return;

    const auto bestBid = std::max_element(
        quotes.begin(), quotes.end(),
        [](const Quote& a, const Quote& b) { return a.bidPrice < b.bidPrice; });
    const auto bestAsk = std::min_element(
        quotes.begin(), quotes.end(),
        [](const Quote& a, const Quote& b) { return a.askPrice < b.askPrice; });

    if (bestBid->exchangeName == bestAsk->exchangeName) return;
    if (bestAsk->askPrice <= 0.0) return;

    const double grossSpreadBps = (bestBid->bidPrice - bestAsk->askPrice) / bestAsk->askPrice * 10000.0;
    const double netSpreadBps = grossSpreadBps - (feeBufferBps_ + slippageBufferBps_);
    if (netSpreadBps < minNetSpreadBps_) return;

    sink_->record(ArbitrageOpportunity{symbol.symbolCode, bestAsk->exchangeName, bestAsk->askPrice,
                                        bestBid->exchangeName, bestBid->bidPrice, grossSpreadBps,
                                        netSpreadBps, std::chrono::system_clock::now()});
}
