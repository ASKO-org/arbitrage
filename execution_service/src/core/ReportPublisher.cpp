#include "core/ReportPublisher.h"

#include <iostream>
#include <stdexcept>

#include <hiredis/hiredis.h>

void ReportPublisher::RedisContextDeleter::operator()(redisContext* context) const {
    if (context) redisFree(context);
}

ReportPublisher::ReportPublisher(const std::string& host, int port, std::string stream)
    : stream_(std::move(stream)), context_(redisConnect(host.c_str(), port)) {
    if (!context_) {
        throw std::runtime_error("ReportPublisher: redisConnect returned null (out of memory?)");
    }
    if (context_->err) {
        throw std::runtime_error("ReportPublisher: failed to connect to Redis at " + host + ":" +
                                  std::to_string(port) + ": " + context_->errstr);
    }
}

ReportPublisher::~ReportPublisher() = default;

void ReportPublisher::publish(const ExecutionReport& report) {
    const std::string payload = toJson(report).dump();

    auto* reply = static_cast<redisReply*>(
        redisCommand(context_.get(), "XADD %s * payload %s", stream_.c_str(), payload.c_str()));
    if (!reply) {
        std::cerr << "[ReportPublisher] XADD failed: " << context_->errstr << "\n";
        return;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[ReportPublisher] XADD rejected: " << (reply->str ? reply->str : "unknown") << "\n";
    }
    freeReplyObject(reply);
}
