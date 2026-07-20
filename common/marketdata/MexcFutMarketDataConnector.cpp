#include "marketdata/MexcFutMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace {
constexpr const char* kWsUrl = "wss://contract.mexc.com/edge";
}  // namespace

MexcFutMarketDataConnector::MexcFutMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()),
      heartbeat_(std::chrono::seconds(15),
                 [this] { webSocket_->send(nlohmann::json{{"method", "ping"}}.dump()); }) {}

MexcFutMarketDataConnector::~MexcFutMarketDataConnector() { stop(); }

std::string MexcFutMarketDataConnector::exchangeName() const { return "MEXC_Fut"; }

void MexcFutMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void MexcFutMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void MexcFutMarketDataConnector::start() {
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
    heartbeat_.start();
}

void MexcFutMarketDataConnector::stop() {
    heartbeat_.stop();
    webSocket_->stop();
}

void MexcFutMarketDataConnector::sendSubscribe() {
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        webSocket_->send(
            nlohmann::json{{"method", "sub.ticker"}, {"param", {{"symbol", it->second}}}}.dump());
    }
}

void MexcFutMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (json.value("channel", "") != "push.ticker") return;  // e.g. a subscribe ack or pong

        const auto& data = json.at("data");
        const auto it = nativeToCanonical_.find(data.at("symbol").get<std::string>());
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = data.at("bid1").get<double>();
        quote.askPrice = data.at("ask1").get<double>();
        // No bid1Size/ask1Size field exists on this channel; qty is left 0.
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape — drop it.
    }
}
