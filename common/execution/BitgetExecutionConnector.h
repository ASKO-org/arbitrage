#pragma once
#include <string>

#include "execution/IExecutionConnector.h"

// Places/cancels spot orders via Bitget's v2 authenticated REST trading API.
// Like Bybit, Bitget's create-order response never carries fill information —
// a signed GET .../orderInfo immediately follows every accepted create to
// resolve the actual fill outcome. Unlike Bybit, Bitget's ACCESS-SIGN is
// base64 (not hex) and requires a third credential, the account's API
// passphrase, alongside key+secret.
class BitgetExecutionConnector : public IExecutionConnector {
public:
    BitgetExecutionConnector(std::string apiKey, std::string apiSecret, std::string apiPassphrase,
                              std::string baseUrl);

    std::string exchangeName() const override;
    OrderResult placeOrder(const Order& order) override;
    void cancelOrder(const std::string& orderId, const std::string& symbol) override;
    OrderResult getOrderStatus(const std::string& orderId, const std::string& symbol) override;
    std::vector<AssetBalance> getBalances() override;
    std::vector<Position> getPositions() override;

private:
    std::string apiKey_;
    std::string apiSecret_;
    std::string apiPassphrase_;
    std::string baseUrl_;
};
