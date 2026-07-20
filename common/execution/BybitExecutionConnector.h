#pragma once
#include <string>

#include "execution/IExecutionConnector.h"

// Places/cancels spot orders via Bybit's v5 authenticated REST trading API.
// Unlike Binance, Bybit's order/create response never carries fill
// information — a signed GET /v5/order/realtime immediately follows every
// accepted create to resolve the actual fill outcome.
class BybitExecutionConnector : public IExecutionConnector {
public:
    BybitExecutionConnector(std::string apiKey, std::string apiSecret, std::string baseUrl);

    std::string exchangeName() const override;
    OrderResult placeOrder(const Order& order) override;
    void cancelOrder(const std::string& orderId, const std::string& symbol) override;
    OrderResult getOrderStatus(const std::string& orderId, const std::string& symbol) override;

private:
    std::string apiKey_;
    std::string apiSecret_;
    std::string baseUrl_;
};
