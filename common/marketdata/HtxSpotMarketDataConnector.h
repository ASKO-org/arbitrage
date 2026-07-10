#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via HTX's "market.<symbol>.bbo" public channel.
// Every push (including the subscribe ack) arrives gzip-compressed inside a
// binary WS frame, even though the payload is plain JSON. Heartbeat is
// reactive, not proactive: the server pushes {"ping":<ms>} and the client
// must reply with a plain (non-gzipped) {"pong":<ms>} — there's no
// client-initiated ping loop here, unlike every other connector so far.
class HtxSpotMarketDataConnector : public IMarketDataConnector {
public:
    HtxSpotMarketDataConnector();
    ~HtxSpotMarketDataConnector() override;

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
