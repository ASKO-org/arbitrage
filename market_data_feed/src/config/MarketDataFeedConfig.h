#pragma once
#include <chrono>
#include <string>

#include "config/Config.h"

// Env-driven tunables for market_data_feed, layered on Config::envOr().
namespace MarketDataFeedConfig {

inline std::string redisHost() { return Config::envOr("REDIS_HOST", "localhost"); }
inline int redisPort() { return std::stoi(Config::envOr("REDIS_PORT", "6379")); }

inline std::string quoteChannel() {
    return Config::envOr("MARKET_DATA_QUOTE_CHANNEL", "market_data:quotes");
}

inline std::chrono::seconds statsReportInterval() {
    return std::chrono::seconds(std::stoi(Config::envOr("MARKET_DATA_STATS_INTERVAL_S", "15")));
}

}  // namespace MarketDataFeedConfig
