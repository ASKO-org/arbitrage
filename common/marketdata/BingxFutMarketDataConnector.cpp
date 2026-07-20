#include "marketdata/BingxFutMarketDataConnector.h"

#include <chrono>
#include <iostream>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "marketdata/Gzip.h"

namespace {
constexpr const char* kWsUrl = "wss://open-api-swap.bingx.com/swap-market";

std::string uniqueId() {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count());
}
}  // namespace

BingxFutMarketDataConnector::BingxFutMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()) {}

BingxFutMarketDataConnector::~BingxFutMarketDataConnector() { stop(); }

std::string BingxFutMarketDataConnector::exchangeName() const { return "BINGX_Fut"; }

void BingxFutMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void BingxFutMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void BingxFutMarketDataConnector::start() {
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

void BingxFutMarketDataConnector::stop() { webSocket_->stop(); }

void BingxFutMarketDataConnector::sendSubscribe() {
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        webSocket_->send(nlohmann::json{{"id", uniqueId()},
                                         {"reqType", "sub"},
                                         {"dataType", it->second + "@bookTicker"}}
                              .dump());
    }
}

void BingxFutMarketDataConnector::handleMessage(const std::string& payload) {
    try {
        const std::string decompressed = gunzip(payload);
        if (decompressed == "Ping") {
            webSocket_->send("Pong");
            return;
        }

        if (!onQuote_) return;
        const auto json = nlohmann::json::parse(decompressed);
        if (!json.contains("data")) return;  // e.g. a subscribe ack

        const auto& data = json.at("data");
        const auto it = nativeToCanonical_.find(data.at("s").get<std::string>());
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
        // Malformed/non-gzip payload or unexpected shape — drop it.
    }
}
