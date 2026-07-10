#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via KuCoin Futures' /contractMarket/tickerV2 topic.
// Same dynamic bullet-token connection flow as KucoinSpotMarketDataConnector,
// but against api-futures.kucoin.com, and unlike spot, this topic is not
// confirmed to support comma-joining multiple symbols — one topic per symbol.
class KucoinFutMarketDataConnector : public IMarketDataConnector {
public:
    KucoinFutMarketDataConnector();
    ~KucoinFutMarketDataConnector() override;

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
