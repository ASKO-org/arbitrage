#pragma once
#include <memory>
#include <string>

#include "models/ExecutionReport.h"

struct redisContext;

// Publishes each order outcome to the execution:reports stream — the
// durable, replayable audit trail independent consumers (a future PnL
// service, alerting, etc.) read via their own consumer groups.
class ReportPublisher {
public:
    // Throws std::runtime_error if the connection to Redis fails.
    ReportPublisher(const std::string& host, int port, std::string stream);
    ~ReportPublisher();

    void publish(const ExecutionReport& report);

private:
    struct RedisContextDeleter {
        void operator()(redisContext* context) const;
    };

    std::string stream_;
    std::unique_ptr<redisContext, RedisContextDeleter> context_;
};
