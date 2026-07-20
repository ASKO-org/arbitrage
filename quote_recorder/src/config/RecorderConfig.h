#pragma once
#include <chrono>
#include <cstddef>
#include <string>

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

// Directory where per-exchange, per-day quote snapshot binary files are
// written (see QuoteSnapshotFileWriter). Created if it doesn't exist.
inline std::string snapshotDir() {
    return Config::envOr("RECORDER_SNAPSHOT_DIR", "./data/quote_snapshots");
}

// Number of buffered rows (per exchange) that triggers a flush to disk.
inline std::size_t snapshotFlushThreshold() {
    return static_cast<std::size_t>(
        std::stoul(Config::envOr("RECORDER_SNAPSHOT_FLUSH_THRESHOLD", "500")));
}

}  // namespace RecorderConfig
