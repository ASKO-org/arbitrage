#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via Binance USD-M futures' combined @bookTicker
// WebSocket streams. Same shape as BinanceMarketDataConnector (spot); the
// push payload carries a few extra futures-only fields (event type, pair,
// transaction/event time, symbol type) that are simply not read.
class BinanceFutMarketDataConnector : public IMarketDataConnector {
public:
    BinanceFutMarketDataConnector();
    ~BinanceFutMarketDataConnector() override;

    std::string exchangeName() const override;
    void subscribe(const std::vector<TrackedSymbol>& symbols) override;
    void setOnQuote(std::function<void(const Quote&)> onQuote) override;
    void start() override;
    void stop() override;

private:
    void handleMessage(const std::string& payload);

    std::vector<TrackedSymbol> symbols_;
    std::unordered_map<std::string, std::string> nativeToCanonical_;
    std::function<void(const Quote&)> onQuote_;
    std::unique_ptr<ix::WebSocket> webSocket_;
};
