#pragma once
#include <string>

#include "config/Config.h"

// Env-driven tunables for execution_service, layered on Config::envOr().
namespace ExecutionConfig {

inline std::string redisHost() { return Config::envOr("REDIS_HOST", "localhost"); }
inline int redisPort() { return std::stoi(Config::envOr("REDIS_PORT", "6379")); }

inline std::string ordersStream() { return Config::envOr("EXECUTION_ORDERS_STREAM", "execution:orders"); }
inline std::string reportsStream() { return Config::envOr("EXECUTION_REPORTS_STREAM", "execution:reports"); }
inline std::string consumerGroup() {
    return Config::envOr("EXECUTION_CONSUMER_GROUP", "execution-workers");
}
inline std::string consumerName() {
    return Config::envOr("EXECUTION_CONSUMER_NAME", "execution-service-1");
}

// Account-wide net-position limit (per venue+symbol) OrderRouter enforces
// across every strategy sharing this account. Deliberately conservative by
// default — raise it explicitly once real limits are decided.
inline double maxAbsPosition() {
    return std::stod(Config::envOr("EXECUTION_MAX_ABS_POSITION", "1000"));
}

// Defaults to each venue's testnet so a service started without credentials
// configured can't accidentally reach the live account.
inline std::string binanceApiKey() { return Config::envOr("BINANCE_API_KEY", ""); }
inline std::string binanceApiSecret() { return Config::envOr("BINANCE_API_SECRET", ""); }
inline std::string binanceBaseUrl() {
    return Config::envOr("BINANCE_BASE_URL", "https://testnet.binance.vision");
}

inline std::string bybitApiKey() { return Config::envOr("BYBIT_API_KEY", ""); }
inline std::string bybitApiSecret() { return Config::envOr("BYBIT_API_SECRET", ""); }
inline std::string bybitBaseUrl() { return Config::envOr("BYBIT_BASE_URL", "https://api-testnet.bybit.com"); }

}  // namespace ExecutionConfig
