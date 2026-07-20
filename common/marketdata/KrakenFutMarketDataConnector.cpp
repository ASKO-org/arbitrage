#include "marketdata/KrakenFutMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace {
constexpr const char* kWsUrl = "wss://futures.kraken.com/ws/v1";
}  // namespace

KrakenFutMarketDataConnector::KrakenFutMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()) {}

KrakenFutMarketDataConnector::~KrakenFutMarketDataConnector() { stop(); }

std::string KrakenFutMarketDataConnector::exchangeName() const { return "KRAKEN_Fut"; }

void KrakenFutMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void KrakenFutMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void KrakenFutMarketDataConnector::start() {
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

void KrakenFutMarketDataConnector::stop() { webSocket_->stop(); }

void KrakenFutMarketDataConnector::sendSubscribe() {
    nlohmann::json productIds = nlohmann::json::array();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it != symbol.nativeSymbols.end()) productIds.push_back(it->second);
    }
    if (productIds.empty()) return;

    webSocket_->send(
        nlohmann::json{{"event", "subscribe"}, {"feed", "ticker"}, {"product_ids", productIds}}.dump());
}

void KrakenFutMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (json.value("feed", "") != "ticker") return;  // e.g. a subscribe/info ack

        const auto it = nativeToCanonical_.find(json.at("product_id").get<std::string>());
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = json.at("bid").get<double>();
        quote.bidQty = json.at("bid_size").get<double>();
        quote.askPrice = json.at("ask").get<double>();
        quote.askQty = json.at("ask_size").get<double>();
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape — drop it.
    }
}
