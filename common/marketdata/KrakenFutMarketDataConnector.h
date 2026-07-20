#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via Kraken Futures' "ticker" feed. This endpoint
// rejects an application-level ping ({"event":"ping"}); it relies purely on
// protocol-level WebSocket ping/pong, which IXWebSocket answers
// automatically, so no HeartbeatTimer is needed here.
class KrakenFutMarketDataConnector : public IMarketDataConnector {
public:
    KrakenFutMarketDataConnector();
    ~KrakenFutMarketDataConnector() override;

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
