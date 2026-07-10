#include "marketdata/KucoinSpotMarketDataConnector.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kBulletUrl = "https://api.kucoin.com/api/v1/bullet-public";
constexpr size_t kMaxSymbolsPerTopic = 100;

std::string uniqueId() {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count());
}

double parseDoubleOr(const nlohmann::json& value, double fallback) {
    if (value.is_null()) return fallback;
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

KucoinSpotMarketDataConnector::KucoinSpotMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()) {}

KucoinSpotMarketDataConnector::~KucoinSpotMarketDataConnector() { stop(); }

std::string KucoinSpotMarketDataConnector::exchangeName() const { return "KUCOIN_Spot"; }

void KucoinSpotMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void KucoinSpotMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void KucoinSpotMarketDataConnector::start() {
    const auto body = HttpClient::post(kBulletUrl);
    const auto json = nlohmann::json::parse(body);
    if (json.value("code", "") != "200000") {
        throw std::runtime_error("KucoinSpotMarketDataConnector: bullet-public error, code=" +
                                  json.value("code", std::string("?")));
    }

    const auto& data = json.at("data");
    const std::string token = data.at("token").get<std::string>();
    const auto& server = data.at("instanceServers").at(0);
    const std::string endpoint = server.at("endpoint").get<std::string>();
    const int pingIntervalMs = server.value("pingInterval", 18000);

    webSocket_->setUrl(endpoint + "?token=" + token + "&connectId=" + uniqueId());
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

    // KuCoin returns the ping cadence dynamically per connection rather than
    // publishing a fixed constant, so the timer has to be built here, not in
    // the constructor.
    heartbeat_ = std::make_unique<HeartbeatTimer>(std::chrono::milliseconds(pingIntervalMs), [this] {
        webSocket_->send(nlohmann::json{{"id", uniqueId()}, {"type", "ping"}}.dump());
    });
    heartbeat_->start();
}

void KucoinSpotMarketDataConnector::stop() {
    if (heartbeat_) heartbeat_->stop();
    webSocket_->stop();
}

void KucoinSpotMarketDataConnector::sendSubscribe() {
    std::vector<std::string> natives;
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it != symbol.nativeSymbols.end()) natives.push_back(it->second);
    }

    // Docs cap a single level1 topic string at 100 comma-joined symbols.
    for (size_t offset = 0; offset < natives.size(); offset += kMaxSymbolsPerTopic) {
        std::string topic = "/spotMarket/level1:";
        for (size_t i = offset; i < std::min(offset + kMaxSymbolsPerTopic, natives.size()); ++i) {
            if (i > offset) topic += ",";
            topic += natives[i];
        }
        webSocket_->send(nlohmann::json{{"id", uniqueId()},
                                         {"type", "subscribe"},
                                         {"topic", topic},
                                         {"response", true}}
                              .dump());
    }
}

void KucoinSpotMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (json.value("type", "") != "message" || json.value("subject", "") != "level1") {
            return;  // e.g. welcome/ack/pong frame
        }

        // Each push's topic is scoped to the one symbol that changed, even
        // though the original subscribe topic may have comma-joined several.
        const std::string topic = json.at("topic").get<std::string>();
        const auto colon = topic.find(':');
        if (colon == std::string::npos) return;
        const std::string nativeSymbol = topic.substr(colon + 1);

        const auto it = nativeToCanonical_.find(nativeSymbol);
        if (it == nativeToCanonical_.end()) return;

        const auto& data = json.at("data");
        const auto& bid = data.at("bids");
        const auto& ask = data.at("asks");
        if (bid.empty() || ask.empty()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = parseDoubleOr(bid.at(0), 0.0);
        quote.bidQty = parseDoubleOr(bid.at(1), 0.0);
        quote.askPrice = parseDoubleOr(ask.at(0), 0.0);
        quote.askQty = parseDoubleOr(ask.at(1), 0.0);
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape — drop it.
    }
}
