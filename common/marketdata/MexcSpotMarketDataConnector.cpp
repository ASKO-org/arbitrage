#include "marketdata/MexcSpotMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "marketdata/MexcProto.h"

namespace {
constexpr const char* kWsUrl = "wss://wbs-api.mexc.com/ws";
// MEXC caps a single connection at 30 subscriptions total.
constexpr size_t kMaxSubscriptions = 30;
}  // namespace

MexcSpotMarketDataConnector::MexcSpotMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()),
      heartbeat_(std::chrono::seconds(15),
                 [this] { webSocket_->send(nlohmann::json{{"method", "PING"}}.dump()); }) {}

MexcSpotMarketDataConnector::~MexcSpotMarketDataConnector() { stop(); }

std::string MexcSpotMarketDataConnector::exchangeName() const { return "MEXC_Spot"; }

void MexcSpotMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void MexcSpotMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void MexcSpotMarketDataConnector::start() {
    webSocket_->setUrl(kWsUrl);
    webSocket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
        if (message->type == ix::WebSocketMessageType::Message) {
            if (message->binary) handleBinaryMessage(message->str);
            // Text messages here are subscribe acks / PONG replies — no
            // quote data to extract from them.
        } else if (message->type == ix::WebSocketMessageType::Open) {
            std::cerr << "[" << exchangeName() << "] connected\n";
            sendSubscribe();
        } else if (message->type == ix::WebSocketMessageType::Close) {
            std::cerr << "[" << exchangeName() << "] disconnected: " << message->closeInfo.reason
                      << "\n";
        } else if (message->type == ix::WebSocketMessageType::Error) {
            std::cerr << "[" << exchangeName() << "] connection error: " << message->errorInfo.reason
                      << "\n";
        }
    });
    webSocket_->start();
    heartbeat_.start();
}

void MexcSpotMarketDataConnector::stop() {
    heartbeat_.stop();
    webSocket_->stop();
}

void MexcSpotMarketDataConnector::sendSubscribe() {
    nlohmann::json params = nlohmann::json::array();
    size_t count = 0;
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        if (count >= kMaxSubscriptions) {
            std::cerr << "[" << exchangeName()
                      << "] watchlist exceeds the 30-subscription per-connection cap; "
                      << "remaining symbols will not be recorded\n";
            break;
        }
        params.push_back("spot@public.aggre.bookTicker.v3.api.pb@100ms@" + it->second);
        ++count;
    }
    if (!params.empty()) {
        webSocket_->send(nlohmann::json{{"method", "SUBSCRIPTION"}, {"params", params}}.dump());
    }
}

void MexcSpotMarketDataConnector::handleBinaryMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        // Outer PushDataV3ApiWrapper: field 3 = symbol (string), field 315 =
        // the embedded PublicAggreBookTickerV3Api submessage.
        std::string nativeSymbol;
        std::string tickerBytes;

        ProtoFieldReader outer(payload);
        while (outer.next()) {
            if (outer.wireType() != 2) continue;
            if (outer.fieldNumber() == 3) {
                nativeSymbol = outer.bytesValue();
            } else if (outer.fieldNumber() == 315) {
                tickerBytes = outer.bytesValue();
            }
        }
        if (nativeSymbol.empty() || tickerBytes.empty()) return;  // e.g. a non-ticker push

        const auto it = nativeToCanonical_.find(nativeSymbol);
        if (it == nativeToCanonical_.end()) return;

        // Submessage: field 1 = bidPrice, 2 = bidQuantity, 3 = askPrice, 4 = askQuantity.
        std::string bidPrice, bidQty, askPrice, askQty;
        ProtoFieldReader ticker(tickerBytes);
        while (ticker.next()) {
            if (ticker.wireType() != 2) continue;
            switch (ticker.fieldNumber()) {
                case 1:
                    bidPrice = ticker.bytesValue();
                    break;
                case 2:
                    bidQty = ticker.bytesValue();
                    break;
                case 3:
                    askPrice = ticker.bytesValue();
                    break;
                case 4:
                    askQty = ticker.bytesValue();
                    break;
                default:
                    break;
            }
        }
        if (bidPrice.empty() || askPrice.empty()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = std::stod(bidPrice);
        quote.bidQty = bidQty.empty() ? 0.0 : std::stod(bidQty);
        quote.askPrice = std::stod(askPrice);
        quote.askQty = askQty.empty() ? 0.0 : std::stod(askQty);
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed/unexpected protobuf shape — drop it.
    }
}
