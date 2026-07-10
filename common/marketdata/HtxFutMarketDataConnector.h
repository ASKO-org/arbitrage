#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via HTX linear-swap's "market.<symbol>.bbo" channel.
// Same gzip-framing and reactive-ping/pong pattern as HtxSpotMarketDataConnector,
// but the push shape differs: no symbol field in the tick (must be parsed
// out of the channel name), and bid/ask are [price, qty] arrays rather than
// flat fields.
class HtxFutMarketDataConnector : public IMarketDataConnector {
public:
    HtxFutMarketDataConnector();
    ~HtxFutMarketDataConnector() override;

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
