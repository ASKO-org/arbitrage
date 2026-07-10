#include "marketdata/HtxSpotMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "marketdata/Gzip.h"

namespace {
constexpr const char* kWsUrl = "wss://api.huobi.pro/ws";

std::string uniqueId() {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count());
}
}  // namespace

HtxSpotMarketDataConnector::HtxSpotMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()) {}

HtxSpotMarketDataConnector::~HtxSpotMarketDataConnector() { stop(); }

std::string HtxSpotMarketDataConnector::exchangeName() const { return "HTX_Spot"; }

void HtxSpotMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void HtxSpotMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void HtxSpotMarketDataConnector::start() {
    webSocket_->setUrl(kWsUrl);
    webSocket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
        if (message->type == ix::WebSocketMessageType::Message) {
            handleMessage(message->str);
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
}

void HtxSpotMarketDataConnector::stop() { webSocket_->stop(); }

void HtxSpotMarketDataConnector::sendSubscribe() {
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        webSocket_->send(
            nlohmann::json{{"sub", "market." + it->second + ".bbo"}, {"id", uniqueId()}}.dump());
    }
}

void HtxSpotMarketDataConnector::handleMessage(const std::string& payload) {
    try {
        const auto json = nlohmann::json::parse(gunzip(payload));

        if (json.contains("ping")) {
            // Reactive heartbeat: HTX pings us; we must pong back in plain
            // (non-gzipped) JSON, not the other way around.
            webSocket_->send(nlohmann::json{{"pong", json.at("ping")}}.dump());
            return;
        }

        if (!onQuote_ || !json.contains("tick")) return;  // e.g. a sub ack

        const auto& tick = json.at("tick");
        const auto it = nativeToCanonical_.find(tick.at("symbol").get<std::string>());
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = tick.at("bid").get<double>();
        quote.bidQty = tick.at("bidSize").get<double>();
        quote.askPrice = tick.at("ask").get<double>();
        quote.askQty = tick.at("askSize").get<double>();
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed/non-gzip payload or unexpected shape — drop it.
    }
}
