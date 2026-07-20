#include "marketdata/QuoteFeedSubscriber.h"

#include <cerrno>
#include <iostream>
#include <stdexcept>

#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

void QuoteFeedSubscriber::RedisContextDeleter::operator()(redisContext* context) const {
    if (context) redisFree(context);
}

QuoteFeedSubscriber::QuoteFeedSubscriber(const std::string& host, int port, std::string channel)
    : channel_(std::move(channel)), context_(redisConnect(host.c_str(), port)) {
    if (!context_) {
        throw std::runtime_error("QuoteFeedSubscriber: redisConnect returned null (out of memory?)");
    }
    if (context_->err) {
        throw std::runtime_error("QuoteFeedSubscriber: failed to connect to Redis at " + host + ":" +
                                  std::to_string(port) + ": " + context_->errstr);
    }

    // A short receive timeout lets run()'s loop wake up periodically to
    // recheck shouldStop() instead of blocking on the socket forever.
    const struct timeval receiveTimeout {
        1, 0
    };
    redisSetTimeout(context_.get(), receiveTimeout);

    auto* reply = static_cast<redisReply*>(redisCommand(context_.get(), "SUBSCRIBE %s", channel_.c_str()));
    if (!reply) {
        throw std::runtime_error("QuoteFeedSubscriber: SUBSCRIBE failed: " + std::string(context_->errstr));
    }
    freeReplyObject(reply);
}

QuoteFeedSubscriber::~QuoteFeedSubscriber() = default;

void QuoteFeedSubscriber::run(const std::function<void(const Quote&)>& handler,
                               const std::function<bool()>& shouldStop) {
    while (!shouldStop()) {
        void* replyVoid = nullptr;
        if (redisGetReply(context_.get(), &replyVoid) != REDIS_OK) {
            if (context_->err == REDIS_ERR_IO && errno == EAGAIN) {
                continue;  // receive timed out with nothing new; recheck shouldStop()
            }
            throw std::runtime_error("QuoteFeedSubscriber: redisGetReply failed: " +
                                      std::string(context_->errstr));
        }

        auto* reply = static_cast<redisReply*>(replyVoid);
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3 && reply->element[0]->str &&
            std::string(reply->element[0]->str) == "message") {
            try {
                const std::string payload = reply->element[2]->str ? reply->element[2]->str : "";
                handler(quoteFromJson(nlohmann::json::parse(payload)));
            } catch (const std::exception& ex) {
                std::cerr << "[QuoteFeedSubscriber] dropping malformed message: " << ex.what() << "\n";
            }
        }
        freeReplyObject(reply);
    }
}
