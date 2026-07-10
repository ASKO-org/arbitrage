#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via Bybit v5 public spot "orderbook.1.*" topics
// (the "tickers.*" topic carries last-price/24h stats only, no bid/ask,
// on spot). Bybit requires an application-level ping roughly every 20s
// or the server drops the connection; HeartbeatTimer covers that.
class BybitMarketDataConnector : public IMarketDataConnector {
public:
    BybitMarketDataConnector();
    ~BybitMarketDataConnector() override;

    std::string exchangeName() const override;
    void subscribe(const std::vector<TrackedSymbol>& symbols) override;
    void setOnQuote(std::function<void(const Quote&)> onQuote) override;
    void start() override;
    void stop() override;

private:
    void handleMessage(const std::string& payload);
    void sendSubscribe();

    std::vector<TrackedSymbol> symbols_;
    std::unordered_map<std::string, std::string> nativeToCanonical_;  // native symbol -> canonical code
    std::function<void(const Quote&)> onQuote_;
    std::unique_ptr<ix::WebSocket> webSocket_;
    HeartbeatTimer heartbeat_;
};
