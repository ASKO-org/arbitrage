#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via CoinEx's "bbo.subscribe" public channel. Every
// frame (including the subscribe ack) is gzip-compressed binary, like HTX.
// The server disconnects an idle connection after ~60s with no traffic;
// the app-level server.ping keeps that from happening (protocol-level ping
// also works per CoinEx's docs, but this matches the JSON-ping pattern
// already used elsewhere in this codebase).
class CoinexSpotMarketDataConnector : public IMarketDataConnector {
public:
    CoinexSpotMarketDataConnector();
    ~CoinexSpotMarketDataConnector() override;

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
