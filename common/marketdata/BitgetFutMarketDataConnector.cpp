#include "marketdata/BitgetFutMarketDataConnector.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace {
constexpr const char* kWsUrl = "wss://ws.bitget.com/v2/ws/public";
constexpr size_t kMaxSymbolsPerMessage = 20;

double parseDoubleOr(const nlohmann::json& value, double fallback) {
    if (value.is_null()) return fallback;
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

BitgetFutMarketDataConnector::BitgetFutMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()),
      heartbeat_(std::chrono::seconds(25), [this] { webSocket_->send("ping"); }) {}

BitgetFutMarketDataConnector::~BitgetFutMarketDataConnector() { stop(); }

std::string BitgetFutMarketDataConnector::exchangeName() const { return "BITGET_Fut"; }

void BitgetFutMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void BitgetFutMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void BitgetFutMarketDataConnector::start() {
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

void BitgetFutMarketDataConnector::stop() {
    heartbeat_.stop();
    webSocket_->stop();
}

void BitgetFutMarketDataConnector::sendSubscribe() {
    std::vector<std::string> natives;
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it != symbol.nativeSymbols.end()) natives.push_back(it->second);
    }

    for (size_t offset = 0; offset < natives.size(); offset += kMaxSymbolsPerMessage) {
        nlohmann::json args = nlohmann::json::array();
        for (size_t i = offset; i < std::min(offset + kMaxSymbolsPerMessage, natives.size()); ++i) {
            args.push_back(
                {{"instType", "USDT-FUTURES"}, {"channel", "books1"}, {"instId", natives[i]}});
        }
        webSocket_->send(nlohmann::json{{"op", "subscribe"}, {"args", args}}.dump());
    }
}

void BitgetFutMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;
    if (payload == "pong") return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (!json.contains("arg") || !json.contains("data")) return;  // e.g. a subscribe ack
        if (json.at("arg").value("channel", "") != "books1") return;

        const auto& dataArray = json.at("data");
        if (dataArray.empty()) return;
        const auto& tick = dataArray.at(0);
        const auto& bid = tick.at("bids");
        const auto& ask = tick.at("asks");
        if (bid.empty() || ask.empty()) return;

        const auto it = nativeToCanonical_.find(json.at("arg").at("instId").get<std::string>());
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = parseDoubleOr(bid.at(0).at(0), 0.0);
        quote.bidQty = parseDoubleOr(bid.at(0).at(1), 0.0);
        quote.askPrice = parseDoubleOr(ask.at(0).at(0), 0.0);
        quote.askQty = parseDoubleOr(ask.at(0).at(1), 0.0);
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape — drop it.
    }
}
