#include "marketdata/QuoteFeedSubscriber.h"

#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

void QuoteFeedSubscriber::RedisContextDeleter::operator()(redisContext* context) const {
    if (context) redisFree(context);
}

QuoteFeedSubscriber::QuoteFeedSubscriber(const std::string& host, int port, std::string channel)
    : host_(host), port_(port), channel_(std::move(channel)) {
    connect();
}

QuoteFeedSubscriber::~QuoteFeedSubscriber() = default;

void QuoteFeedSubscriber::connect() {
    context_.reset(redisConnect(host_.c_str(), port_));
    if (!context_) {
        throw std::runtime_error("QuoteFeedSubscriber: redisConnect returned null (out of memory?)");
    }
    if (context_->err) {
        throw std::runtime_error("QuoteFeedSubscriber: failed to connect to Redis at " + host_ + ":" +
                                  std::to_string(port_) + ": " + context_->errstr);
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

void QuoteFeedSubscriber::run(const std::function<void(const Quote&)>& handler,
                               const std::function<bool()>& shouldStop) {
    while (!shouldStop()) {
        void* replyVoid = nullptr;
        if (redisGetReply(context_.get(), &replyVoid) != REDIS_OK) {
            if (context_->err == REDIS_ERR_IO && errno == EAGAIN) {
                continue;  // receive timed out with nothing new; recheck shouldStop()
            }

            // Anything else means this connection is unusable — e.g. Redis
            // closed it (a pub/sub client that falls behind gets
            // disconnected once it exceeds Redis's client-output-buffer
            // limit) or a transient network drop. Reconnect and
            // re-subscribe instead of either busy-spinning on a dead socket
            // or tearing down the whole process over something recoverable.
            std::cerr << "[QuoteFeedSubscriber] connection lost (" << context_->errstr
                       << ") -- reconnecting\n";
            while (!shouldStop()) {
                try {
                    connect();
                    std::cerr << "[QuoteFeedSubscriber] reconnected\n";
                    break;
                } catch (const std::exception& ex) {
                    std::cerr << "[QuoteFeedSubscriber] reconnect failed: " << ex.what()
                               << " -- retrying in 1s\n";
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
            continue;
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
