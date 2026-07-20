#include "marketdata/QuoteFeedPublisher.h"

#include <iostream>
#include <stdexcept>

#include <hiredis/hiredis.h>

void QuoteFeedPublisher::RedisContextDeleter::operator()(redisContext* context) const {
    if (context) redisFree(context);
}

QuoteFeedPublisher::QuoteFeedPublisher(const std::string& host, int port, std::string channel)
    : channel_(std::move(channel)), context_(redisConnect(host.c_str(), port)) {
    if (!context_) {
        throw std::runtime_error("QuoteFeedPublisher: redisConnect returned null (out of memory?)");
    }
    if (context_->err) {
        throw std::runtime_error("QuoteFeedPublisher: failed to connect to Redis at " + host + ":" +
                                  std::to_string(port) + ": " + context_->errstr);
    }
}

QuoteFeedPublisher::~QuoteFeedPublisher() = default;

void QuoteFeedPublisher::publish(const Quote& quote) {
    const std::string payload = toJson(quote).dump();

    auto* reply = static_cast<redisReply*>(
        redisCommand(context_.get(), "PUBLISH %s %s", channel_.c_str(), payload.c_str()));
    if (!reply) {
        std::cerr << "[QuoteFeedPublisher] publish failed: " << context_->errstr << "\n";
        return;
    }
    freeReplyObject(reply);
}
