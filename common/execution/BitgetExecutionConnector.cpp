#include "execution/BitgetExecutionConnector.h"

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

// Bitget's Get-Order-Info response wasn't confirmed from the docs alone to
// return `data` as a single object vs. a one-element array for a
// single-order query — handle either shape rather than assume.
const nlohmann::json& firstOrderInfo(const nlohmann::json& data) {
    static const nlohmann::json empty = nlohmann::json::object();
    if (!data.is_array()) return data;
    return data.empty() ? empty : data.at(0);
}

// Bitget's plain spot place-order endpoint has no base/quote-unit override
// (unlike its margin endpoints, or Bybit's marketUnit) — "size" on a market
// BUY always means quote-currency amount to spend, so a market buy for a
// specific base-asset qty has to be converted using the current price, same
// approach ccxt's bitget adapter uses. Public endpoint, no auth needed.
double fetchAskPrice(const std::string& baseUrl, const std::string& symbol) {
    const std::string responseBody = HttpClient::get(baseUrl + "/api/v2/spot/market/tickers?symbol=" + symbol);
    const auto json = nlohmann::json::parse(responseBody);
    if (json.value("code", "") != "00000") {
        throw std::runtime_error("Bitget ticker error: " + json.value("msg", "unknown error"));
    }
    const auto& data = json.at("data");
    if (data.empty()) {
        throw std::runtime_error("Bitget ticker: no data for symbol " + symbol);
    }
    return std::stod(data.at(0).value("askPr", "0"));
}

}  // namespace

BitgetExecutionConnector::BitgetExecutionConnector(std::string apiKey, std::string apiSecret,
                                                    std::string apiPassphrase, std::string baseUrl)
    : apiKey_(std::move(apiKey)),
      apiSecret_(std::move(apiSecret)),
      apiPassphrase_(std::move(apiPassphrase)),
      baseUrl_(std::move(baseUrl)) {}

std::string BitgetExecutionConnector::exchangeName() const { return "BITGET_Spot"; }

OrderResult BitgetExecutionConnector::placeOrder(const Order& order) {
    std::string sizeStr = std::to_string(order.qty);
    if (order.type == OrderType::Market && order.side == OrderSide::Buy) {
        try {
            const double askPrice = fetchAskPrice(baseUrl_, order.symbol);
            sizeStr = std::to_string(order.qty * askPrice);
        } catch (const std::exception& ex) {
            return OrderResult{false, 0.0, 0.0, false,
                                std::string("failed to price market buy for base-qty sizing: ") + ex.what()};
        }
    }

    // clientOid carries order.orderId through as-is, so a retried submission
    // is deduped by Bitget itself rather than creating a second order.
    nlohmann::json body = {
        {"symbol", order.symbol},
        {"side", order.side == OrderSide::Buy ? "buy" : "sell"},
        {"orderType", order.type == OrderType::Market ? "market" : "limit"},
        {"size", sizeStr},
        {"clientOid", order.orderId},
    };
    if (order.type == OrderType::Limit) {
        body["price"] = std::to_string(order.price);
        body["force"] = "gtc";
    }
    const std::string bodyStr = body.dump();

    const std::string requestPath = "/api/v2/spot/trade/place-order";
    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string signature =
        HmacSigner::hmacSha256Base64(apiSecret_, timestamp + "POST" + requestPath + bodyStr);

    const std::vector<std::string> headers = {
        "ACCESS-KEY: " + apiKey_,
        "ACCESS-SIGN: " + signature,
        "ACCESS-TIMESTAMP: " + timestamp,
        "ACCESS-PASSPHRASE: " + apiPassphrase_,
        "Content-Type: application/json",
    };

    try {
        const std::string responseBody = HttpClient::request("POST", baseUrl_ + requestPath, bodyStr, headers);
        const auto json = nlohmann::json::parse(responseBody);
        if (json.value("code", "") != "00000") {
            return OrderResult{false, 0.0, 0.0, false, json.value("msg", "unknown error")};
        }
        // The create-order response never carries fill info on Bitget v2 —
        // an immediate status query resolves the actual outcome.
        return getOrderStatus(order.orderId, order.symbol);
    } catch (const std::exception& ex) {
        return OrderResult{false, 0.0, 0.0, false, ex.what()};
    }
}

OrderResult BitgetExecutionConnector::getOrderStatus(const std::string& orderId, const std::string& symbol) {
    (void)symbol;  // Bitget looks orders up by clientOid alone, not (symbol, id) like Bybit.
    const std::string requestPath = "/api/v2/spot/trade/orderInfo";
    const std::string queryString = "clientOid=" + orderId;
    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string signature =
        HmacSigner::hmacSha256Base64(apiSecret_, timestamp + "GET" + requestPath + "?" + queryString);

    const std::vector<std::string> headers = {
        "ACCESS-KEY: " + apiKey_,
        "ACCESS-SIGN: " + signature,
        "ACCESS-TIMESTAMP: " + timestamp,
        "ACCESS-PASSPHRASE: " + apiPassphrase_,
    };

    // A failure here (transport error, order not found yet) doesn't mean the
    // order wasn't placed — it means the fill state couldn't be confirmed.
    // Reporting accepted=true/filledQty=0 rather than accepted=false keeps
    // the order non-terminal instead of OrderRouter recording a false
    // rejection for an order that may already be resting or filled.
    try {
        const std::string responseBody =
            HttpClient::request("GET", baseUrl_ + requestPath + "?" + queryString, "", headers);
        const auto json = nlohmann::json::parse(responseBody);
        if (json.value("code", "") != "00000") {
            return OrderResult{true, 0.0, 0.0, false, ""};
        }
        const auto& info = firstOrderInfo(json.at("data"));
        if (info.empty()) {
            return OrderResult{true, 0.0, 0.0, false, ""};
        }
        const double baseVolume = std::stod(info.value("baseVolume", "0"));
        const double avgPrice = baseVolume > 0.0 ? std::stod(info.value("priceAvg", "0")) : 0.0;
        const bool fullyFilled = info.value("status", "") == "filled";
        return OrderResult{true, baseVolume, avgPrice, fullyFilled, ""};
    } catch (const std::exception&) {
        return OrderResult{true, 0.0, 0.0, false, ""};
    }
}

std::vector<AssetBalance> BitgetExecutionConnector::getBalances() {
    // hold_only skips the hundreds of zero-balance coins Bitget would
    // otherwise return for "all".
    const std::string requestPath = "/api/v2/spot/account/assets";
    const std::string queryString = "assetType=hold_only";
    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string signature =
        HmacSigner::hmacSha256Base64(apiSecret_, timestamp + "GET" + requestPath + "?" + queryString);

    const std::vector<std::string> headers = {
        "ACCESS-KEY: " + apiKey_,
        "ACCESS-SIGN: " + signature,
        "ACCESS-TIMESTAMP: " + timestamp,
        "ACCESS-PASSPHRASE: " + apiPassphrase_,
    };

    const std::string responseBody = HttpClient::request("GET", baseUrl_ + requestPath + "?" + queryString, "", headers);
    const auto json = nlohmann::json::parse(responseBody);
    if (json.value("code", "") != "00000") {
        throw std::runtime_error("Bitget account-assets error: " + json.value("msg", "unknown error"));
    }

    std::vector<AssetBalance> balances;
    for (const auto& entry : json.at("data")) {
        const double available = std::stod(entry.value("available", "0"));
        // `locked` is a separate bucket from `frozen` (used for things like
        // OTC/staking reserves) — near-always 0 for a plain trading account,
        // folded in here for a conservative "not currently tradable" figure.
        const double frozen = std::stod(entry.value("frozen", "0"));
        const double locked = std::stod(entry.value("locked", "0"));
        balances.push_back(AssetBalance{entry.value("coin", ""), available, frozen + locked});
    }
    return balances;
}

std::vector<Position> BitgetExecutionConnector::getPositions() {
    const std::string requestPath = "/api/v2/mix/position/all-position";
    const std::string queryString = "productType=USDT-FUTURES&marginCoin=USDT";
    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string signature =
        HmacSigner::hmacSha256Base64(apiSecret_, timestamp + "GET" + requestPath + "?" + queryString);

    const std::vector<std::string> headers = {
        "ACCESS-KEY: " + apiKey_,
        "ACCESS-SIGN: " + signature,
        "ACCESS-TIMESTAMP: " + timestamp,
        "ACCESS-PASSPHRASE: " + apiPassphrase_,
    };

    const std::string responseBody =
        HttpClient::request("GET", baseUrl_ + requestPath + "?" + queryString, "", headers);
    const auto json = nlohmann::json::parse(responseBody);
    if (json.value("code", "") != "00000") {
        throw std::runtime_error("Bitget all-position error: " + json.value("msg", "unknown error"));
    }

    std::vector<Position> positions;
    for (const auto& entry : json.at("data")) {
        const double size = std::stod(entry.value("total", "0"));
        if (size == 0.0) continue;
        positions.push_back(Position{
            entry.value("symbol", ""),
            entry.value("holdSide", ""),
            size,
            std::stod(entry.value("openPriceAvg", "0")),
            std::stod(entry.value("markPrice", "0")),
            std::stod(entry.value("unrealizedPL", "0")),
            std::stod(entry.value("leverage", "0")),
        });
    }
    return positions;
}

void BitgetExecutionConnector::cancelOrder(const std::string& orderId, const std::string& symbol) {
    const nlohmann::json body = {
        {"symbol", symbol},
        {"clientOid", orderId},
    };
    const std::string bodyStr = body.dump();

    const std::string requestPath = "/api/v2/spot/trade/cancel-order";
    const std::string timestamp = std::to_string(currentTimeMs());
    const std::string signature =
        HmacSigner::hmacSha256Base64(apiSecret_, timestamp + "POST" + requestPath + bodyStr);

    const std::vector<std::string> headers = {
        "ACCESS-KEY: " + apiKey_,
        "ACCESS-SIGN: " + signature,
        "ACCESS-TIMESTAMP: " + timestamp,
        "ACCESS-PASSPHRASE: " + apiPassphrase_,
        "Content-Type: application/json",
    };
    HttpClient::request("POST", baseUrl_ + requestPath, bodyStr, headers);
}
