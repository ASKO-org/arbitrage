#include <iostream>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "config/ExecutionConfig.h"
#include "execution/BitgetExecutionConnector.h"
#include "execution/BybitExecutionConnector.h"
#include "execution/IExecutionConnector.h"
#include "security/SecretsStore.h"

// Prints current spot balances, across every venue with credentials
// configured, as a single JSON array to stdout — one line per asset:
// {"venue": "...", "asset": "...", "free": ..., "locked": ...}. Consumed by
// execution_viewer via subprocess, the same boundary secrets_viewer uses
// for secrets_cli — the web app never touches SecretsStore directly.
int main() {
    try {
        SecretsStore secrets(ExecutionConfig::secretsMasterKeyPath(), ExecutionConfig::secretsFilePath());

        std::vector<std::unique_ptr<IExecutionConnector>> connectors;
        if (secrets.has("bybit_api_key") && secrets.has("bybit_api_secret")) {
            connectors.push_back(std::make_unique<BybitExecutionConnector>(
                secrets.get("bybit_api_key"), secrets.get("bybit_api_secret"), ExecutionConfig::bybitBaseUrl()));
        }
        if (secrets.has("bitget_api_key") && secrets.has("bitget_api_secret") &&
            secrets.has("bitget_api_passphrase")) {
            connectors.push_back(std::make_unique<BitgetExecutionConnector>(
                secrets.get("bitget_api_key"), secrets.get("bitget_api_secret"),
                secrets.get("bitget_api_passphrase"), ExecutionConfig::bitgetBaseUrl()));
        }

        nlohmann::json result = nlohmann::json::array();
        for (const auto& connector : connectors) {
            try {
                for (const auto& balance : connector->getBalances()) {
                    result.push_back({
                        {"venue", connector->exchangeName()},
                        {"asset", balance.asset},
                        {"free", balance.free},
                        {"locked", balance.locked},
                    });
                }
            } catch (const std::exception& ex) {
                result.push_back({
                    {"venue", connector->exchangeName()},
                    {"error", ex.what()},
                });
            }
        }
        std::cout << result.dump() << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
