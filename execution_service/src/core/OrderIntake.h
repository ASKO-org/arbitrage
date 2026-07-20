#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "models/Order.h"

struct redisContext;

// Consumes Order requests from a Redis stream via a consumer group, so
// multiple strategies can XADD concurrently and a crash between reading an
// entry and acking it leaves the entry claimable again instead of lost.
class OrderIntake {
public:
    // Throws std::runtime_error if the connection to Redis fails.
    OrderIntake(const std::string& host, int port, std::string stream, std::string group,
                std::string consumerName, std::chrono::milliseconds minIdleTimeForReclaim = std::chrono::seconds(30));
    ~OrderIntake();

    // Creates the consumer group (and stream, via MKSTREAM) if it doesn't
    // exist yet. Safe to call every startup — an existing group is a no-op.
    void ensureGroup();

    // Blocks, reading new entries and reclaiming any pending entry idle
    // longer than minIdleTimeForReclaim (left behind by a crashed consumer),
    // invoking handler(order) for each. An entry is XACKed only after
    // handler returns without throwing — a throw leaves it unacked so it's
    // reclaimed and retried later instead of silently dropped. Returns once
    // shouldStop() returns true.
    void run(const std::function<void(const Order&)>& handler, const std::function<bool()>& shouldStop);

private:
    struct RedisContextDeleter {
        void operator()(redisContext* context) const;
    };

    void reclaimStalePending(const std::function<void(const Order&)>& handler);
    void ack(const std::string& entryId);

    std::string stream_;
    std::string group_;
    std::string consumerName_;
    std::chrono::milliseconds minIdleTimeForReclaim_;
    std::unique_ptr<redisContext, RedisContextDeleter> context_;
};
