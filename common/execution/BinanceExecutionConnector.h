#pragma once
#include <string>

#include "execution/IExecutionConnector.h"

// Places/cancels spot orders via Binance's authenticated REST trading API
// (https://binance-docs.github.io/apidocs/spot/en/#new-order-trade). Every
// request is HMAC-SHA256 signed with apiSecret over its query string.
class BinanceExecutionConnector : public IExecutionConnector {
public:
    BinanceExecutionConnector(std::string apiKey, std::string apiSecret, std::string baseUrl);

    std::string exchangeName() const override;
    OrderResult placeOrder(const Order& order) override;
    void cancelOrder(const std::string& orderId, const std::string& symbol) override;
    OrderResult getOrderStatus(const std::string& orderId, const std::string& symbol) override;

private:
    std::string apiKey_;
    std::string apiSecret_;
    std::string baseUrl_;
};
