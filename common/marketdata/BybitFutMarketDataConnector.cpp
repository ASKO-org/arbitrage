#include "marketdata/BybitFutMarketDataConnector.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

BybitFutMarketDataConnector::BybitFutMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()),
      heartbeat_(std::chrono::seconds(20), [this] {
          webSocket_->send(nlohmann::json{{"op", "ping"}}.dump());
      }) {}

BybitFutMarketDataConnector::~BybitFutMarketDataConnector() { stop(); }

std::string BybitFutMarketDataConnector::exchangeName() const { return "BYBIT_Fut"; }

void BybitFutMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void BybitFutMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void BybitFutMarketDataConnector::start() {
    webSocket_->setUrl("wss://stream.bybit.com/v5/public/linear");
    webSocket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
        if (message->type == ix::WebSocketMessageType::Open) {
            std::cerr << "[" << exchangeName() << "] connected\n";
            sendSubscribe();
        } else if (message->type == ix::WebSocketMessageType::Message) {
            handleMessage(message->str);
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

void BybitFutMarketDataConnector::stop() {
    heartbeat_.stop();
    webSocket_->stop();
}

void BybitFutMarketDataConnector::sendSubscribe() {
    std::vector<std::string> topics;
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        topics.push_back("orderbook.1." + it->second);
    }

    constexpr size_t kMaxTopicsPerRequest = 10;
    for (size_t offset = 0; offset < topics.size(); offset += kMaxTopicsPerRequest) {
        nlohmann::json args = nlohmann::json::array();
        for (size_t i = offset; i < std::min(offset + kMaxTopicsPerRequest, topics.size()); ++i) {
            args.push_back(topics[i]);
        }
        webSocket_->send(nlohmann::json{{"op", "subscribe"}, {"args", args}}.dump());
    }
}

void BybitFutMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (!json.contains("topic") || !json.contains("data")) {
            if (json.value("op", "") == "subscribe" && !json.value("success", true)) {
                std::cerr << "[" << exchangeName() << "] subscribe rejected: " << json.value("ret_msg", "")
                          << "\n";
            }
            return;  // e.g. a subscribe ack or pong
        }

        const auto& data = json.at("data");
        const auto& bids = data.value("b", nlohmann::json::array());
        const auto& asks = data.value("a", nlohmann::json::array());
        if (bids.empty() || asks.empty()) return;

        const auto it = nativeToCanonical_.find(data.at("s").get<std::string>());
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = std::stod(bids.at(0).at(0).get<std::string>());
        quote.bidQty = std::stod(bids.at(0).at(1).get<std::string>());
        quote.askPrice = std::stod(asks.at(0).at(0).get<std::string>());
        quote.askQty = std::stod(asks.at(0).at(1).get<std::string>());
        // "ts" is Bybit's own push timestamp (ms since epoch) on the message
        // envelope; falls back to local receipt time if it's ever absent.
        quote.exchangeTimestamp = exchangeTimestampOrNow(json.value("ts", int64_t{0}));
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape — drop it.
    }
}
