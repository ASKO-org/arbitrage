#pragma once
#include <memory>
#include <string>

#include "models/Order.h"

struct redisContext;

// What any strategy (or the execution_cli tool) links against to send an
// order: serializes it to JSON and XADDs it to the execution:orders stream.
// Never talks to execution_service directly — the stream is durable, so this
// call succeeds independent of whether execution_service is up at that instant.
class OrderSubmitter {
public:
    // Throws std::runtime_error if the connection to Redis fails.
    OrderSubmitter(const std::string& host, int port, std::string stream);
    ~OrderSubmitter();

    // Returns the Redis stream entry id assigned to this order.
    std::string submit(const Order& order);

private:
    struct RedisContextDeleter {
        void operator()(redisContext* context) const;
    };

    std::string stream_;
    std::unique_ptr<redisContext, RedisContextDeleter> context_;
};
