#include "marketdata/KrakenSpotMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace {
constexpr const char* kWsUrl = "wss://ws.kraken.com/v2";

double parseDoubleOr(const nlohmann::json& value, double fallback) {
    if (value.is_null()) return fallback;
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

KrakenSpotMarketDataConnector::KrakenSpotMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()) {}

KrakenSpotMarketDataConnector::~KrakenSpotMarketDataConnector() { stop(); }

std::string KrakenSpotMarketDataConnector::exchangeName() const { return "KRAKEN_Spot"; }

void KrakenSpotMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void KrakenSpotMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void KrakenSpotMarketDataConnector::start() {
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

void KrakenSpotMarketDataConnector::stop() { webSocket_->stop(); }

void KrakenSpotMarketDataConnector::sendSubscribe() {
    nlohmann::json symbols = nlohmann::json::array();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it != symbol.nativeSymbols.end()) symbols.push_back(it->second);
    }
    if (symbols.empty()) return;

    webSocket_->send(nlohmann::json{{"method", "subscribe"},
                                     {"params",
                                      {{"channel", "ticker"}, {"symbol", symbols}, {"event_trigger", "bbo"}}}}
                          .dump());
}

void KrakenSpotMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (json.value("channel", "") != "ticker") return;  // e.g. a heartbeat/status/ack message

        for (const auto& tick : json.at("data")) {
            const auto it = nativeToCanonical_.find(tick.at("symbol").get<std::string>());
            if (it == nativeToCanonical_.end()) continue;

            Quote quote;
            quote.exchangeName = exchangeName();
            quote.symbolCode = it->second;
            quote.bidPrice = parseDoubleOr(tick.at("bid"), 0.0);
            quote.bidQty = parseDoubleOr(tick.at("bid_qty"), 0.0);
            quote.askPrice = parseDoubleOr(tick.at("ask"), 0.0);
            quote.askQty = parseDoubleOr(tick.at("ask_qty"), 0.0);
            quote.exchangeTimestamp = std::chrono::system_clock::now();
            quote.receivedAt = std::chrono::steady_clock::now();
            onQuote_(quote);
        }
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape — drop it.
    }
}
