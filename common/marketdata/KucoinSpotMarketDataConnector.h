#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via KuCoin's /spotMarket/level1 topic. KuCoin has no
// static public WS URL: a REST POST to bullet-public returns a one-time
// token, connection endpoint, and a dynamic ping interval that must be
// honored (read at connect time, not hardcoded).
class KucoinSpotMarketDataConnector : public IMarketDataConnector {
public:
    KucoinSpotMarketDataConnector();
    ~KucoinSpotMarketDataConnector() override;

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
    std::unique_ptr<HeartbeatTimer> heartbeat_;
};
