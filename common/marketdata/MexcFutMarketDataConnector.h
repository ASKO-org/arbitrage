#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via MEXC Futures' plain-JSON "sub.ticker" channel.
// Unlike every other connector here, this channel carries only bid1/ask1
// price — no accompanying size field exists on this push, so bidQty/askQty
// are always left at 0 rather than guessed.
class MexcFutMarketDataConnector : public IMarketDataConnector {
public:
    MexcFutMarketDataConnector();
    ~MexcFutMarketDataConnector() override;

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
