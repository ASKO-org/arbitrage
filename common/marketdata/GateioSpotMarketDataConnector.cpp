#include "marketdata/GateioSpotMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace {
constexpr const char* kWsUrl = "wss://api.gateio.ws/ws/v4/";

double parseDoubleOr(const nlohmann::json& value, double fallback) {
    if (value.is_null()) return fallback;
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}

long nowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

GateioSpotMarketDataConnector::GateioSpotMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()),
      heartbeat_(std::chrono::seconds(20), [this] {
          webSocket_->send(
              nlohmann::json{{"time", nowEpochSeconds()}, {"channel", "spot.ping"}}.dump());
      }) {}

GateioSpotMarketDataConnector::~GateioSpotMarketDataConnector() { stop(); }

std::string GateioSpotMarketDataConnector::exchangeName() const { return "GATEIO_Spot"; }

void GateioSpotMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void GateioSpotMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void GateioSpotMarketDataConnector::start() {
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

void GateioSpotMarketDataConnector::stop() {
    heartbeat_.stop();
    webSocket_->stop();
}

void GateioSpotMarketDataConnector::sendSubscribe() {
    nlohmann::json payload = nlohmann::json::array();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it != symbol.nativeSymbols.end()) payload.push_back(it->second);
    }
    if (payload.empty()) return;

    webSocket_->send(nlohmann::json{{"time", nowEpochSeconds()},
                                     {"channel", "spot.book_ticker"},
                                     {"event", "subscribe"},
                                     {"payload", payload}}
                          .dump());
}

void GateioSpotMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (json.value("channel", "") != "spot.book_ticker" || json.value("event", "") != "update") {
            return;  // e.g. a subscribe/ping ack
        }

        const auto& result = json.at("result");
        const auto it = nativeToCanonical_.find(result.at("s").get<std::string>());
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = parseDoubleOr(result.at("b"), 0.0);
        quote.bidQty = parseDoubleOr(result.at("B"), 0.0);
        quote.askPrice = parseDoubleOr(result.at("a"), 0.0);
        quote.askQty = parseDoubleOr(result.at("A"), 0.0);
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape — drop it.
    }
}
