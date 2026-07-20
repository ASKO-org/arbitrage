#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via BingX Futures' "<SYMBOL>@bookTicker" channel.
// Same gzip-framing and one-symbol-per-subscribe-message pattern as spot,
// but heartbeat here is a bare (non-JSON) "Ping"/"Pong" text exchange,
// reactive and on a much shorter ~5s cadence than spot's ~30s JSON one.
class BingxFutMarketDataConnector : public IMarketDataConnector {
public:
    BingxFutMarketDataConnector();
    ~BingxFutMarketDataConnector() override;

    std::string exchangeName() const override;
    void subscribe(const std::vector<TrackedSymbol>& symbols) override;
    void setOnQuote(std::function<void(const Quote&)> onQuote) override;
    void start() override;
    void stop() override;

private:
    void handleMessage(const std::string& payload);
    void sendSubscribe();

    std::vector<TrackedSymbol> symbols_;
    std::unordered_map<std::string, std::string> nativeToCanonical_;
    std::function<void(const Quote&)> onQuote_;
    std::unique_ptr<ix::WebSocket> webSocket_;
};
