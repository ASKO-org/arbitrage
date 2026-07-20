#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/HeartbeatTimer.h"
#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via MEXC's protobuf-framed
// "spot@public.aggre.bookTicker" channel. Market-data pushes arrive as
// binary protobuf frames; subscribe acks and PONG replies are plain JSON
// text. Parsed via the minimal ProtoFieldReader rather than pulling in a
// full protobuf dependency for one exchange.
class MexcSpotMarketDataConnector : public IMarketDataConnector {
public:
    MexcSpotMarketDataConnector();
    ~MexcSpotMarketDataConnector() override;

    std::string exchangeName() const override;
    void subscribe(const std::vector<TrackedSymbol>& symbols) override;
    void setOnQuote(std::function<void(const Quote&)> onQuote) override;
    void start() override;
    void stop() override;

private:
    void handleBinaryMessage(const std::string& payload);
    void sendSubscribe();

    std::vector<TrackedSymbol> symbols_;
    std::unordered_map<std::string, std::string> nativeToCanonical_;
    std::function<void(const Quote&)> onQuote_;
    std::unique_ptr<ix::WebSocket> webSocket_;
    HeartbeatTimer heartbeat_;
};
