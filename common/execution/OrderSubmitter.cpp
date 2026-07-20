#include "execution/OrderSubmitter.h"

#include <stdexcept>

#include <hiredis/hiredis.h>

void OrderSubmitter::RedisContextDeleter::operator()(redisContext* context) const {
    if (context) redisFree(context);
}

OrderSubmitter::OrderSubmitter(const std::string& host, int port, std::string stream)
    : stream_(std::move(stream)), context_(redisConnect(host.c_str(), port)) {
    if (!context_) {
        throw std::runtime_error("OrderSubmitter: redisConnect returned null (out of memory?)");
    }
    if (context_->err) {
        throw std::runtime_error("OrderSubmitter: failed to connect to Redis at " + host + ":" +
                                  std::to_string(port) + ": " + context_->errstr);
    }
}

OrderSubmitter::~OrderSubmitter() = default;

std::string OrderSubmitter::submit(const Order& order) {
    const std::string payload = toJson(order).dump();

    auto* reply = static_cast<redisReply*>(
        redisCommand(context_.get(), "XADD %s * payload %s", stream_.c_str(), payload.c_str()));
    if (!reply) {
        throw std::runtime_error("OrderSubmitter: XADD failed: " + std::string(context_->errstr));
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        const std::string message = reply->str ? reply->str : "unknown error";
        freeReplyObject(reply);
        throw std::runtime_error("OrderSubmitter: XADD rejected by Redis: " + message);
    }

    std::string entryId = reply->str ? reply->str : "";
    freeReplyObject(reply);
    return entryId;
}
