#pragma once
#include <cstdlib>
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
inline std::string binanceBaseUrl() {
    return Config::envOr("BINANCE_BASE_URL", "https://testnet.binance.vision");
}
inline std::string bybitBaseUrl() { return Config::envOr("BYBIT_BASE_URL", "https://api-testnet.bybit.com"); }

// The actual API keys/secrets are no longer read from plain env vars — see
// SecretsStore (common/security/SecretsStore.h). These two paths are not
// secrets themselves, just where to find the (encrypted) secret and the
// (separate, outside-the-repo) master key that decrypts it.
inline std::string secretsMasterKeyPath() {
    const char* home = std::getenv("HOME");
    const std::string defaultPath = (home ? std::string(home) : std::string(".")) +
                                     "/.secrets/instrument_loader.key";
    return Config::envOr("SECRETS_MASTER_KEY_PATH", defaultPath);
}
inline std::string secretsFilePath() {
    return Config::envOr("SECRETS_FILE_PATH", "secrets/exchange_keys.enc.json");
}

// How long a credential can go unrotated before execution_service warns
// loudly about it at startup. Doesn't block startup — a missed rotation
// reminder is a much smaller problem than an unplanned trading outage.
inline int secretsMaxAgeDays() {
    return std::stoi(Config::envOr("SECRETS_MAX_AGE_DAYS", "90"));
}

}  // namespace ExecutionConfig
