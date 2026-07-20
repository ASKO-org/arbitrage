#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via CoinEx Futures' "bbo.subscribe" channel. Same
// gzip-framing and heartbeat pattern as CoinexSpotMarketDataConnector, just
// a different host.
class CoinexFutMarketDataConnector : public IMarketDataConnector {
public:
    CoinexFutMarketDataConnector();
    ~CoinexFutMarketDataConnector() override;

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
