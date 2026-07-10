#pragma once
#include <memory>
#include <string>

#include "core/OpportunitySink.h"

struct redisContext;

// Publishes each opportunity as JSON to a Redis pub/sub channel, so any
// number of external consumers (a demo web gateway, alerting, logging) can
// watch the feed without this service knowing they exist.
class RedisOpportunitySink : public OpportunitySink {
public:
    // Throws std::runtime_error if the connection to Redis fails.
    RedisOpportunitySink(const std::string& host, int port, std::string channel);
    ~RedisOpportunitySink() override;

    void record(const ArbitrageOpportunity& opportunity) override;

private:
    struct RedisContextDeleter {
        void operator()(redisContext* context) const;
    };

    std::string channel_;
    std::unique_ptr<redisContext, RedisContextDeleter> context_;
};
