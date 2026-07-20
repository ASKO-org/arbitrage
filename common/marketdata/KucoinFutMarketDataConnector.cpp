#include "marketdata/KucoinFutMarketDataConnector.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "network/HttpClient.h"

namespace {
constexpr const char* kBulletUrl = "https://api-futures.kucoin.com/api/v1/bullet-public";

// Verified experimentally: sending one subscribe message per symbol with no
// delay (fine at a few dozen symbols) causes KuCoin Futures to abnormally
// close the connection once the watchlist grows into the hundreds (observed
// at 650 symbols). This isn't the same failure shape as GATEIO_Fut's
// silently-dropped oversized batch — here the connection itself gets torn
// down, pointing at a message-rate limit rather than a message-size one.
// Comma-joining topics like KucoinSpotMarketDataConnector does isn't used
// here since (per the class comment) it's unconfirmed whether the futures
// topic supports it — throttling the existing one-topic-per-symbol calls is
// the safer fix that doesn't risk silently breaking the subscription format.
constexpr std::chrono::milliseconds kSubscribeThrottle{200};

std::string uniqueId() {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count());
}

double parseDoubleOr(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.contains(key) || obj.at(key).is_null()) return fallback;
    const auto& value = obj.at(key);
    return value.is_string() ? std::stod(value.get<std::string>()) : value.get<double>();
}
}  // namespace

KucoinFutMarketDataConnector::KucoinFutMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()) {}

KucoinFutMarketDataConnector::~KucoinFutMarketDataConnector() { stop(); }

std::string KucoinFutMarketDataConnector::exchangeName() const { return "KUCOIN_Fut"; }

void KucoinFutMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void KucoinFutMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void KucoinFutMarketDataConnector::start() {
    const auto body = HttpClient::post(kBulletUrl);
    const auto json = nlohmann::json::parse(body);
    if (json.value("code", "") != "200000") {
        throw std::runtime_error("KucoinFutMarketDataConnector: bullet-public error, code=" +
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

    heartbeat_ = std::make_unique<HeartbeatTimer>(std::chrono::milliseconds(pingIntervalMs), [this] {
        webSocket_->send(nlohmann::json{{"id", uniqueId()}, {"type", "ping"}}.dump());
    });
    heartbeat_->start();
}

void KucoinFutMarketDataConnector::stop() {
    if (heartbeat_) heartbeat_->stop();
    webSocket_->stop();
}

void KucoinFutMarketDataConnector::sendSubscribe() {
    // Runs on a detached thread, not inline on the WebSocket's own I/O
    // callback thread — otherwise sleeping between sends here would also
    // block this connector from processing incoming messages (pings, acks)
    // for the whole throttled burst.
    std::thread([this] {
        for (const auto& symbol : symbols_) {
            const auto it = symbol.nativeSymbols.find(exchangeName());
            if (it == symbol.nativeSymbols.end()) continue;
            webSocket_->send(nlohmann::json{{"id", uniqueId()},
                                             {"type", "subscribe"},
                                             {"topic", "/contractMarket/tickerV2:" + it->second},
                                             {"response", true}}
                                  .dump());
            std::this_thread::sleep_for(kSubscribeThrottle);
        }
    }).detach();
}

void KucoinFutMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (json.value("type", "") != "message" || json.value("subject", "") != "tickerV2") {
            return;  // e.g. welcome/ack/pong frame
        }

        const auto& data = json.at("data");
        const auto it = nativeToCanonical_.find(data.at("symbol").get<std::string>());
        if (it == nativeToCanonical_.end()) return;

        Quote quote;
        quote.exchangeName = exchangeName();
        quote.symbolCode = it->second;
        quote.bidPrice = parseDoubleOr(data, "bestBidPrice", 0.0);
        quote.bidQty = parseDoubleOr(data, "bestBidSize", 0.0);
        quote.askPrice = parseDoubleOr(data, "bestAskPrice", 0.0);
        quote.askQty = parseDoubleOr(data, "bestAskSize", 0.0);
        quote.exchangeTimestamp = std::chrono::system_clock::now();
        quote.receivedAt = std::chrono::steady_clock::now();
        onQuote_(quote);
    } catch (const std::exception&) {
        // Malformed or unexpected payload shape — drop it.
    }
}
