#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via Bitget's "books1" public channel. Bitget uses one
// shared WS host for every market; instType distinguishes spot from futures
// inside the subscribe message, not the URL. Heartbeat is a bare (non-JSON)
// "ping" text frame, expecting a bare "pong" back.
class BitgetSpotMarketDataConnector : public IMarketDataConnector {
public:
    BitgetSpotMarketDataConnector();
    ~BitgetSpotMarketDataConnector() override;

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
