#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via BingX's "<SYMBOL>@bookTicker" channel (one
// symbol per subscribe message — there's no batched-topics form here).
// Every frame is gzip-compressed. Heartbeat is reactive: the server sends a
// JSON {"ping":token,"time":...} roughly every 30s and expects the same
// token/time echoed back in a {"pong":...} reply — no client-initiated ping.
class BingxSpotMarketDataConnector : public IMarketDataConnector {
public:
    BingxSpotMarketDataConnector();
    ~BingxSpotMarketDataConnector() override;

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
