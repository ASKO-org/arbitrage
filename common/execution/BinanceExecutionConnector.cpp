#include "execution/BinanceExecutionConnector.h"

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

BinanceExecutionConnector::BinanceExecutionConnector(std::string apiKey, std::string apiSecret,
                                                       std::string baseUrl)
    : apiKey_(std::move(apiKey)), apiSecret_(std::move(apiSecret)), baseUrl_(std::move(baseUrl)) {}

std::string BinanceExecutionConnector::exchangeName() const { return "BINANCE_Spot"; }

OrderResult BinanceExecutionConnector::placeOrder(const Order& order) {
    // newClientOrderId carries order.orderId through as-is, so a retried
    // submission (e.g. after a crash before we recorded the outcome) is
    // deduped by Binance itself rather than creating a second order.
    std::string query = "symbol=" + order.symbol + "&side=" + (order.side == OrderSide::Buy ? "BUY" : "SELL") +
                         "&type=" + (order.type == OrderType::Market ? "MARKET" : "LIMIT") +
                         "&quantity=" + std::to_string(order.qty);
    if (order.type == OrderType::Limit) {
        query += "&price=" + std::to_string(order.price) + "&timeInForce=GTC";
    }
    query += "&newClientOrderId=" + order.orderId + "&newOrderRespType=FULL" +
             "&recvWindow=5000&timestamp=" + std::to_string(currentTimeMs());

    const std::string signature = HmacSigner::hmacSha256Hex(apiSecret_, query);
    const std::string url = baseUrl_ + "/api/v3/order?" + query + "&signature=" + signature;

    try {
        const std::string responseBody = HttpClient::request("POST", url, "", {"X-MBX-APIKEY: " + apiKey_});
        const auto json = nlohmann::json::parse(responseBody);
        const std::string status = json.value("status", "");

        double filledQty = 0.0;
        double notional = 0.0;
        if (json.contains("fills")) {
            for (const auto& fill : json.at("fills")) {
                const double qty = std::stod(fill.value("qty", "0"));
                const double price = std::stod(fill.value("price", "0"));
                filledQty += qty;
                notional += qty * price;
            }
        }
        const double avgPrice = filledQty > 0.0 ? notional / filledQty : 0.0;

        if (status == "REJECTED" || status == "EXPIRED") {
            return OrderResult{false, 0.0, 0.0, false, "Binance order status: " + status};
        }
        return OrderResult{true, filledQty, avgPrice, status == "FILLED", ""};
    } catch (const std::exception& ex) {
        // A 4xx here (bad price, insufficient balance, ...) surfaces as a
        // thrown std::runtime_error from HttpClient with Binance's error
        // body embedded — treat it as a clean rejection, not a transport
        // failure, so OrderRouter records it and moves on.
        return OrderResult{false, 0.0, 0.0, false, ex.what()};
    }
}

void BinanceExecutionConnector::cancelOrder(const std::string& orderId, const std::string& symbol) {
    const std::string query = "symbol=" + symbol + "&origClientOrderId=" + orderId +
                               "&recvWindow=5000&timestamp=" + std::to_string(currentTimeMs());
    const std::string signature = HmacSigner::hmacSha256Hex(apiSecret_, query);
    const std::string url = baseUrl_ + "/api/v3/order?" + query + "&signature=" + signature;
    HttpClient::request("DELETE", url, "", {"X-MBX-APIKEY: " + apiKey_});
}

OrderResult BinanceExecutionConnector::getOrderStatus(const std::string& orderId, const std::string& symbol) {
    const std::string query = "symbol=" + symbol + "&origClientOrderId=" + orderId +
                               "&recvWindow=5000&timestamp=" + std::to_string(currentTimeMs());
    const std::string signature = HmacSigner::hmacSha256Hex(apiSecret_, query);
    const std::string url = baseUrl_ + "/api/v3/order?" + query + "&signature=" + signature;

    try {
        const std::string responseBody = HttpClient::request("GET", url, "", {"X-MBX-APIKEY: " + apiKey_});
        const auto json = nlohmann::json::parse(responseBody);
        const double executedQty = std::stod(json.value("executedQty", "0"));
        const double cumQuoteQty = std::stod(json.value("cummulativeQuoteQty", "0"));
        const double avgPrice = executedQty > 0.0 ? cumQuoteQty / executedQty : 0.0;
        const std::string status = json.value("status", "");
        return OrderResult{true, executedQty, avgPrice, status == "FILLED", ""};
    } catch (const std::exception& ex) {
        return OrderResult{false, 0.0, 0.0, false, ex.what()};
    }
}
