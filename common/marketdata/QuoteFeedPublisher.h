#pragma once
#include <memory>
#include <string>

#include "marketdata/Quote.h"

struct redisContext;

// Publishes each quote as JSON to a Redis pub/sub channel. Fire-and-forget
// by design: an occasional dropped quote (subscriber briefly disconnected)
// is fine since a fresher one is always seconds away, unlike order flow
// (execution_service's ReportPublisher/OrderIntake) which needs the durable
// Streams+consumer-group treatment because losing one matters.
class QuoteFeedPublisher {
public:
    // Throws std::runtime_error if the connection to Redis fails.
    QuoteFeedPublisher(const std::string& host, int port, std::string channel);
    ~QuoteFeedPublisher();

    void publish(const Quote& quote);

private:
    struct RedisContextDeleter {
        void operator()(redisContext* context) const;
    };

    std::string channel_;
    std::unique_ptr<redisContext, RedisContextDeleter> context_;
};
