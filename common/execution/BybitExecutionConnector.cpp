#include "execution/BybitExecutionConnector.h"

#include <chrono>

#include <nlohmann/json.hpp>

#include "execution/HmacSigner.h"
#include "network/HttpClient.h"

namespace {

long long currentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

BybitExecutionConnector::BybitExecutionConnector(std::string apiKey, std::string apiSecret, std::string baseUrl)
    : apiKey_(std::move(apiKey)), apiSecret_(std::move(apiSecret)), baseUrl_(std::move(baseUrl)) {}

std::string BybitExecutionConnector::exchangeName() const { return "BYBIT_Spot"; }

OrderResult BybitExecutionConnector::placeOrder(const Order& order) {
    // orderLinkId carries order.orderId through as-is, so a retried
    // submission is deduped by Bybit itself rather than creating a second order.
    nlohmann::json body = {
        {"category", "spot"},
        {"symbol", order.symbol},
        {"side", order.side == OrderSide::Buy ? "Buy" : "Sell"},
        {"orderType", order.type == OrderType::Market ? "Market" : "Limit"},
        {"qty", std::to_string(order.qty)},
        {"orderLinkId", order.orderId},
    };
    if (order.type == OrderType::Limit) {
        body["price"] = std::to_string(order.price);
        body["timeInForce"] = "GTC";
    } else {
        // Market orders default to spending-by-quote-value for buys (sells
        // already default to base-coin) — force base-coin sizing explicitly
        // so qty means the same thing regardless of side.
        body["marketUnit"] = "baseCoin";
    }
    const std::string bodyStr = body.dump();

    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string recvWindow = "5000";
    const std::string signature = HmacSigner::hmacSha256Hex(apiSecret_, timestamp + apiKey_ + recvWindow + bodyStr);

    const std::vector<std::string> headers = {
        "X-BAPI-API-KEY: " + apiKey_,
        "X-BAPI-TIMESTAMP: " + timestamp,
        "X-BAPI-RECV-WINDOW: " + recvWindow,
        "X-BAPI-SIGN: " + signature,
        "Content-Type: application/json",
    };

    try {
        const std::string responseBody =
            HttpClient::request("POST", baseUrl_ + "/v5/order/create", bodyStr, headers);
        const auto json = nlohmann::json::parse(responseBody);
        if (json.value("retCode", -1) != 0) {
            return OrderResult{false, 0.0, 0.0, false, json.value("retMsg", "unknown error")};
        }
        // The create-order response never carries fill info on Bybit v5 —
        // an immediate status query resolves the actual outcome.
        return getOrderStatus(order.orderId, order.symbol);
    } catch (const std::exception& ex) {
        return OrderResult{false, 0.0, 0.0, false, ex.what()};
    }
}

OrderResult BybitExecutionConnector::getOrderStatus(const std::string& orderId, const std::string& symbol) {
    const std::string queryString = "category=spot&symbol=" + symbol + "&orderLinkId=" + orderId;
    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string recvWindow = "5000";
    const std::string signature =
        HmacSigner::hmacSha256Hex(apiSecret_, timestamp + apiKey_ + recvWindow + queryString);

    const std::vector<std::string> headers = {
        "X-BAPI-API-KEY: " + apiKey_,
        "X-BAPI-TIMESTAMP: " + timestamp,
        "X-BAPI-RECV-WINDOW: " + recvWindow,
        "X-BAPI-SIGN: " + signature,
    };

    // A failure here (transport error, order not found yet) doesn't mean
    // the order wasn't placed — it means the fill state couldn't be
    // confirmed. Reporting accepted=true/filledQty=0 rather than
    // accepted=false keeps the order non-terminal (OrderStatus::Acked)
    // instead of OrderRouter recording a false rejection for an order that
    // may already be resting or filled on the exchange.
    try {
        const std::string responseBody =
            HttpClient::request("GET", baseUrl_ + "/v5/order/realtime?" + queryString, "", headers);
        const auto json = nlohmann::json::parse(responseBody);
        if (json.value("retCode", -1) != 0) {
            return OrderResult{true, 0.0, 0.0, false, ""};
        }
        const auto& list = json.at("result").at("list");
        if (list.empty()) {
            return OrderResult{true, 0.0, 0.0, false, ""};
        }
        const auto& info = list.at(0);
        const double cumExecQty = std::stod(info.value("cumExecQty", "0"));
        const double avgPrice = cumExecQty > 0.0 ? std::stod(info.value("avgPrice", "0")) : 0.0;
        const bool fullyFilled = info.value("orderStatus", "") == "Filled";
        return OrderResult{true, cumExecQty, avgPrice, fullyFilled, ""};
    } catch (const std::exception&) {
        return OrderResult{true, 0.0, 0.0, false, ""};
    }
}

std::vector<AssetBalance> BybitExecutionConnector::getBalances() {
    // UNIFIED is the only account type Bybit's docs list for this endpoint
    // today — legacy Classic-account types are effectively gone.
    const std::string queryString = "accountType=UNIFIED";
    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string recvWindow = "5000";
    const std::string signature =
        HmacSigner::hmacSha256Hex(apiSecret_, timestamp + apiKey_ + recvWindow + queryString);

    const std::vector<std::string> headers = {
        "X-BAPI-API-KEY: " + apiKey_,
        "X-BAPI-TIMESTAMP: " + timestamp,
        "X-BAPI-RECV-WINDOW: " + recvWindow,
        "X-BAPI-SIGN: " + signature,
    };

    const std::string responseBody =
        HttpClient::request("GET", baseUrl_ + "/v5/account/wallet-balance?" + queryString, "", headers);
    const auto json = nlohmann::json::parse(responseBody);
    if (json.value("retCode", -1) != 0) {
        throw std::runtime_error("Bybit wallet-balance error: " + json.value("retMsg", "unknown error"));
    }

    std::vector<AssetBalance> balances;
    const auto& list = json.at("result").at("list");
    if (list.empty()) return balances;
    for (const auto& coinEntry : list.at(0).at("coin")) {
        const double walletBalance = std::stod(coinEntry.value("walletBalance", "0"));
        const double locked = std::stod(coinEntry.value("locked", "0"));
        // ~0 for a spot-only coin — only nonzero if margin/derivatives are
        // also using this same coin as collateral.
        const double positionIM = std::stod(coinEntry.value("totalPositionIM", "0"));
        const double orderIM = std::stod(coinEntry.value("totalOrderIM", "0"));
        if (walletBalance == 0.0 && locked == 0.0) continue;
        balances.push_back(AssetBalance{coinEntry.value("coin", ""), walletBalance - locked - positionIM - orderIM,
                                         locked});
    }
    return balances;
}

std::vector<Position> BybitExecutionConnector::getPositions() {
    // linear = USDT-margined perpetuals; settleCoin with no symbol returns
    // every open position in one call instead of querying symbol by symbol.
    const std::string queryString = "category=linear&settleCoin=USDT";
    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string recvWindow = "5000";
    const std::string signature =
        HmacSigner::hmacSha256Hex(apiSecret_, timestamp + apiKey_ + recvWindow + queryString);

    const std::vector<std::string> headers = {
        "X-BAPI-API-KEY: " + apiKey_,
        "X-BAPI-TIMESTAMP: " + timestamp,
        "X-BAPI-RECV-WINDOW: " + recvWindow,
        "X-BAPI-SIGN: " + signature,
    };

    const std::string responseBody =
        HttpClient::request("GET", baseUrl_ + "/v5/position/list?" + queryString, "", headers);
    const auto json = nlohmann::json::parse(responseBody);
    if (json.value("retCode", -1) != 0) {
        throw std::runtime_error("Bybit position-list error: " + json.value("retMsg", "unknown error"));
    }

    std::vector<Position> positions;
    for (const auto& entry : json.at("result").at("list")) {
        // Bybit returns one entry per symbol even when flat (size "0"),
        // especially under hedge/positionIdx mode.
        const double size = std::stod(entry.value("size", "0"));
        if (size == 0.0) continue;
        positions.push_back(Position{
            entry.value("symbol", ""),
            entry.value("side", "") == "Buy" ? "long" : "short",
            size,
            std::stod(entry.value("avgPrice", "0")),
            std::stod(entry.value("markPrice", "0")),
            std::stod(entry.value("unrealisedPnl", "0")),
            std::stod(entry.value("leverage", "0")),
        });
    }
    return positions;
}

void BybitExecutionConnector::cancelOrder(const std::string& orderId, const std::string& symbol) {
    const nlohmann::json body = {
        {"category", "spot"},
        {"symbol", symbol},
        {"orderLinkId", orderId},
    };
    const std::string bodyStr = body.dump();

    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string recvWindow = "5000";
    const std::string signature = HmacSigner::hmacSha256Hex(apiSecret_, timestamp + apiKey_ + recvWindow + bodyStr);

    const std::vector<std::string> headers = {
        "X-BAPI-API-KEY: " + apiKey_,
        "X-BAPI-TIMESTAMP: " + timestamp,
        "X-BAPI-RECV-WINDOW: " + recvWindow,
        "X-BAPI-SIGN: " + signature,
        "Content-Type: application/json",
    };
    HttpClient::request("POST", baseUrl_ + "/v5/order/cancel", bodyStr, headers);
}
