#include "marketdata/HtxFutMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "marketdata/Gzip.h"

namespace {
constexpr const char* kWsUrl = "wss://api.hbdm.com/linear-swap-ws";

std::string uniqueId() {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count());
}
}  // namespace

HtxFutMarketDataConnector::HtxFutMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()) {}

HtxFutMarketDataConnector::~HtxFutMarketDataConnector() { stop(); }

std::string HtxFutMarketDataConnector::exchangeName() const { return "HTX_Fut"; }

void HtxFutMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void HtxFutMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void HtxFutMarketDataConnector::start() {
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

void HtxFutMarketDataConnector::stop() { webSocket_->stop(); }

void HtxFutMarketDataConnector::sendSubscribe() {
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        webSocket_->send(
            nlohmann::json{{"sub", "market." + it->second + ".bbo"}, {"id", uniqueId()}}.dump());
    }
}

void HtxFutMarketDataConnector::handleMessage(const std::string& payload) {
    try {
        const auto json = nlohmann::json::parse(gunzip(payload));

        if (json.contains("ping")) {
            webSocket_->send(nlohmann::json{{"pong", json.at("ping")}}.dump());
            return;
        }

        if (!onQuote_ || !json.contains("tick")) return;  // e.g. a sub ack

        // No symbol field inside tick here — parse it out of "market.X.bbo".
        const std::string channel = json.at("ch").get<std::string>();
        constexpr size_t kPrefixLen = 7;  // "market."
        const auto suffixPos = channel.find(".bbo");
        if (suffixPos == std::string::npos || suffixPos <= kPrefixLen) return;
        const std::string nativeSymbol = channel.substr(kPrefixLen, suffixPos - kPrefixLen);

        const auto it = nativeToCanonical_.find(nativeSymbol);
        if (it == nativeToCanonical_.end()) return;

        const auto& tick = json.at("tick");
        const auto& bid = tick.at("bid");
        const auto& ask = tick.at("ask");
        if (bid.empty() || ask.empty()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = bid.at(0).get<double>();
        quote.bidQty = bid.at(1).get<double>();
        quote.askPrice = ask.at(0).get<double>();
        quote.askQty = ask.at(1).get<double>();
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed/non-gzip payload or unexpected shape — drop it.
    }
}
