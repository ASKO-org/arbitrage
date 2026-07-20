#include "marketdata/GateioFutMarketDataConnector.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

namespace {
constexpr const char* kWsUrl = "wss://fx-ws.gateio.ws/v4/ws/usdt";

// Gate.io Futures silently drops a futures.book_ticker subscribe request
// once too many symbols are batched into one message: the ack comes back
// with no "result" field (instead of {"status":"success"}) and no ticker
// data ever follows, with no error/close event to signal the failure.
// Verified experimentally (52 symbols in one message: silently dropped: 1
// symbol: acked with status success, data streamed within milliseconds).
// Chunking well below that keeps every batch inside whatever the real limit
// is, mirroring KucoinSpotMarketDataConnector's kMaxSymbolsPerTopic pattern
// for the same class of exchange-side limit.
constexpr std::size_t kMaxSymbolsPerSubscribe = 10;

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

GateioFutMarketDataConnector::GateioFutMarketDataConnector()
    : webSocket_(std::make_unique<ix::WebSocket>()),
      heartbeat_(std::chrono::seconds(20), [this] {
          webSocket_->send(
              nlohmann::json{{"time", nowEpochSeconds()}, {"channel", "futures.ping"}}.dump());
      }) {}

GateioFutMarketDataConnector::~GateioFutMarketDataConnector() { stop(); }

std::string GateioFutMarketDataConnector::exchangeName() const { return "GATEIO_Fut"; }

void GateioFutMarketDataConnector::subscribe(const std::vector<TrackedSymbol>& symbols) {
    symbols_ = symbols;
    nativeToCanonical_.clear();
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it == symbol.nativeSymbols.end()) continue;
        nativeToCanonical_[it->second] = symbol.symbolCode;
    }
}

void GateioFutMarketDataConnector::setOnQuote(std::function<void(const Quote&)> onQuote) {
    onQuote_ = std::move(onQuote);
}

void GateioFutMarketDataConnector::start() {
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

void GateioFutMarketDataConnector::stop() {
    heartbeat_.stop();
    webSocket_->stop();
}

void GateioFutMarketDataConnector::sendSubscribe() {
    std::vector<std::string> natives;
    for (const auto& symbol : symbols_) {
        const auto it = symbol.nativeSymbols.find(exchangeName());
        if (it != symbol.nativeSymbols.end()) natives.push_back(it->second);
    }

    for (std::size_t offset = 0; offset < natives.size(); offset += kMaxSymbolsPerSubscribe) {
        nlohmann::json payload = nlohmann::json::array();
        for (std::size_t i = offset; i < std::min(offset + kMaxSymbolsPerSubscribe, natives.size()); ++i) {
            payload.push_back(natives[i]);
        }
        webSocket_->send(nlohmann::json{{"time", nowEpochSeconds()},
                                         {"channel", "futures.book_ticker"},
                                         {"event", "subscribe"},
                                         {"payload", payload}}
                              .dump());
    }
}

void GateioFutMarketDataConnector::handleMessage(const std::string& payload) {
    if (!onQuote_) return;

    try {
        const auto json = nlohmann::json::parse(payload);
        if (json.value("channel", "") != "futures.book_ticker" || json.value("event", "") != "update") {
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
