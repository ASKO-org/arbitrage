#pragma once
#include <memory>
#include <unordered_map>

#include "marketdata/IMarketDataConnector.h"

namespace ix {
class WebSocket;
}

// Streams best bid/ask via Kraken WS v2's "ticker" channel (event_trigger
// "bbo" so it only fires on an actual best-bid/offer change, not every
// trade). No client-initiated heartbeat is needed — Kraken keeps the
// connection alive on its own as long as a channel stays subscribed.
class KrakenSpotMarketDataConnector : public IMarketDataConnector {
public:
    KrakenSpotMarketDataConnector();
    ~KrakenSpotMarketDataConnector() override;

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
