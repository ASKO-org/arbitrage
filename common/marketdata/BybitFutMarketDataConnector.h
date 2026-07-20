#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via Bybit v5 public linear (USDT-margined perpetual)
// "orderbook.1.*" topics. Same push shape and app-level ping requirement as
// BybitMarketDataConnector (spot); linear's own docs claim no topic-count
// cap per subscribe message, but this still batches at 10 for consistency
// with the documented spot limit rather than relying on undocumented behavior.
class BybitFutMarketDataConnector : public IMarketDataConnector {
public:
    BybitFutMarketDataConnector();
    ~BybitFutMarketDataConnector() override;

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
    HeartbeatTimer heartbeat_;
};
