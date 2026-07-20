#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via OKX's "bbo-tbt" public channel. OKX uses one
// shared public WS host for both spot and swap; only instId format differs.
// Heartbeat is a bare (non-JSON) "ping" text frame, expecting a bare "pong".
class OkxSpotMarketDataConnector : public IMarketDataConnector {
public:
    OkxSpotMarketDataConnector();
    ~OkxSpotMarketDataConnector() override;

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
