#include "marketdata/BinanceMarketDataConnector.h"

#include <algorithm>
#include <cctype>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace {
std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}  // namespace

BinanceMarketDataConnector::BinanceMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()) {}

BinanceMarketDataConnector::~BinanceMarketDataConnector() { stop(); }

std::string BinanceMarketDataConnector::exchangeName() const { return "BINANCE_Spot"; }

void BinanceMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[toLower(it->second)] = symbol.symbolCode;
    }
}

void BinanceMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void BinanceMarketDataConnector::start() {
    std::string streams;
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        if (!streams.empty()) streams += "/";
        streams += toLower(it->second) + "@bookTicker";
    }

    webSocket_->setUrl("wss://stream.binance.com:9443/stream?streams=" + streams);
    webSocket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
        if (message->type == ix::WebSocketMessageType::Message) {
            handleMessage(message->str);
        } else if (message->type == ix::WebSocketMessageType::Open) {
            std::cerr << "[" << exchangeName() << "] connected\n";
        } else if (message->type == ix::WebSocketMessageType::Close) {
            std::cerr << "[" << exchangeName() << "] disconnected: " << message->closeInfo.reason << "\n";
        } else if (message->type == ix::WebSocketMessageType::Error) {
            std::cerr << "[" << exchangeName() << "] connection error: " << message->errorInfo.reason << "\n";
        }
    });
    webSocket_->start();
}

void BinanceMarketDataConnector::stop() { webSocket_->stop(); }

void BinanceMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        const auto& data = json.at("data");
        const auto it = nativeToCanonical_.find(toLower(data.at("s").get<std::string>()));
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = std::stod(data.at("b").get<std::string>());
        quote.bidQty = std::stod(data.at("B").get<std::string>());
        quote.askPrice = std::stod(data.at("a").get<std::string>());
        quote.askQty = std::stod(data.at("A").get<std::string>());
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape (e.g. a control frame) — drop it.
    }
}
