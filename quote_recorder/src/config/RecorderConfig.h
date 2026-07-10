#pragma once
#include <chrono>

#include "config/Config.h"

// Env-driven tunables for quote_recorder, layered on Config::envOr().
namespace RecorderConfig {

inline std::chrono::milliseconds snapshotInterval() {
    return std::chrono::milliseconds(
        std::stoi(Config::envOr("RECORDER_SNAPSHOT_INTERVAL_MS", "2000")));
}

inline std::chrono::milliseconds quoteTtl() {
    return std::chrono::milliseconds(std::stoi(Config::envOr("RECORDER_QUOTE_TTL_MS", "2000")));
}

}  // namespace RecorderConfig
