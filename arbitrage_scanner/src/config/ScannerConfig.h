#pragma once
#include <chrono>

#include "config/Config.h"

// Env-driven tunables for the arbitrage scanner, layered on Config::envOr().
namespace ScannerConfig {

inline double feeBufferBps() { return std::stod(Config::envOr("SCANNER_FEE_BUFFER_BPS", "20")); }

inline double slippageBufferBps() {
    return std::stod(Config::envOr("SCANNER_SLIPPAGE_BUFFER_BPS", "5"));
}

inline double minNetSpreadBps() {
    return std::stod(Config::envOr("SCANNER_MIN_NET_SPREAD_BPS", "5"));
}

inline std::chrono::milliseconds quoteTtl() {
    return std::chrono::milliseconds(std::stoi(Config::envOr("SCANNER_QUOTE_TTL_MS", "2000")));
}

inline std::chrono::milliseconds detectionInterval() {
    return std::chrono::milliseconds(
        std::stoi(Config::envOr("SCANNER_DETECTION_INTERVAL_MS", "200")));
}

inline std::string redisHost() { return Config::envOr("REDIS_HOST", "localhost"); }

inline int redisPort() { return std::stoi(Config::envOr("REDIS_PORT", "6379")); }

inline std::string redisChannel() {
    return Config::envOr("REDIS_CHANNEL", "arbitrage:opportunities");
}

}  // namespace ScannerConfig
