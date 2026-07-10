#include "core/RedisOpportunitySink.h"

#include <iostream>
#include <stdexcept>

#include <hiredis/hiredis.h>

void RedisOpportunitySink::RedisContextDeleter::operator()(redisContext* context) const {
    if (context) redisFree(context);
}

RedisOpportunitySink::RedisOpportunitySink(const std::string& host, int port, std::string channel)
    : channel_(std::move(channel)), context_(redisConnect(host.c_str(), port)) {
    if (!context_) {
        throw std::runtime_error("redisConnect returned null (out of memory?)");
    }
    if (context_->err) {
        throw std::runtime_error("Failed to connect to Redis at " + host + ":" +
                                  std::to_string(port) + ": " + context_->errstr);
    }
}

RedisOpportunitySink::~RedisOpportunitySink() = default;

void RedisOpportunitySink::record(const ArbitrageOpportunity& opportunity) {
    const std::string payload = toJson(opportunity).dump();

    auto* reply = static_cast<redisReply*>(
        redisCommand(context_.get(), "PUBLISH %s %s", channel_.c_str(), payload.c_str()));
    if (!reply) {
        std::cerr << "[Redis] publish failed: " << context_->errstr << "\n";
        return;
    }
    freeReplyObject(reply);
}
