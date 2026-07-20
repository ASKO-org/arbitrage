#include "marketdata/CoinexSpotMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "marketdata/Gzip.h"

namespace {
constexpr const char* kWsUrl = "wss://socket.coinex.com/v2/spot";

double parseDoubleOr(const nlohmann::json& value, double fallback) {
    if (value.is_null()) return fallback;
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

CoinexSpotMarketDataConnector::CoinexSpotMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()),
      heartbeat_(std::chrono::seconds(30), [this] {
          webSocket_->send(nlohmann::json{{"method", "server.ping"}, {"params", nlohmann::json::object()},
                                           {"id", 0}}
                                .dump());
      }) {}

CoinexSpotMarketDataConnector::~CoinexSpotMarketDataConnector() { stop(); }

std::string CoinexSpotMarketDataConnector::exchangeName() const { return "COINEX_Spot"; }

void CoinexSpotMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void CoinexSpotMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void CoinexSpotMarketDataConnector::start() {
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

void CoinexSpotMarketDataConnector::stop() {
    heartbeat_.stop();
    webSocket_->stop();
}

void CoinexSpotMarketDataConnector::sendSubscribe() {
    nlohmann::json markets = nlohmann::json::array();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it != symbol.nativeSymbols.end()) markets.push_back(it->second);
    }
    if (markets.empty()) return;

    webSocket_->send(
        nlohmann::json{{"method", "bbo.subscribe"}, {"params", {{"market_list", markets}}}, {"id", 1}}
            .dump());
}

void CoinexSpotMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(gunzip(payload));
        if (json.value("method", "") != "bbo.update") return;  // e.g. a subscribe/ping ack

        const auto& data = json.at("data");
        const auto it = nativeToCanonical_.find(data.at("market").get<std::string>());
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = parseDoubleOr(data.at("best_bid_price"), 0.0);
        quote.bidQty = parseDoubleOr(data.at("best_bid_size"), 0.0);
        quote.askPrice = parseDoubleOr(data.at("best_ask_price"), 0.0);
        quote.askQty = parseDoubleOr(data.at("best_ask_size"), 0.0);
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed/non-gzip payload or unexpected shape — drop it.
    }
}
