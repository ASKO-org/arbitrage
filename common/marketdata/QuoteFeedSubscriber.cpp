#include "marketdata/QuoteFeedSubscriber.h"

#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

namespace {
// Real exchange-to-us latency is ~100-300ms even in the worst case
// (measured). A quote this much older than "now" can't be genuine network
// delay — it can only mean the connection died and we're draining a stale
// backlog that built up in hiredis's own read buffer before the drop.
constexpr auto kStaleQuoteThreshold = std::chrono::seconds(15);
}  // namespace

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

void QuoteFeedSubscriber::reconnectWithRetry(const std::function<bool()>& shouldStop) {
    while (!shouldStop()) {
        try {
            connect();
            std::cerr << "[QuoteFeedSubscriber] reconnected on '" << channel_ << "'\n";
            return;
        } catch (const std::exception& ex) {
            std::cerr << "[QuoteFeedSubscriber] reconnect failed: " << ex.what() << " -- retrying in 1s\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
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
            std::cerr << "[QuoteFeedSubscriber] connection lost on '" << channel_ << "' ("
                       << context_->errstr << ") -- reconnecting\n";
            reconnectWithRetry(shouldStop);
            continue;
        }

        auto* reply = static_cast<redisReply*>(replyVoid);
        const bool isMessage = reply->type == REDIS_REPLY_ARRAY && reply->elements == 3 &&
                                reply->element[0]->str && std::string(reply->element[0]->str) == "message";
        std::string payload;
        if (isMessage) payload = reply->element[2]->str ? reply->element[2]->str : "";
        freeReplyObject(reply);
        if (!isMessage) continue;

        try {
            const Quote quote = quoteFromJson(nlohmann::json::parse(payload));

            // A quote can only be this old if we're processing a backlog
            // hiredis already had buffered locally from before the
            // connection actually died — Redis's own disconnect (above)
            // doesn't fire until *after* that buffer is exhausted, which
            // can take a long time (or never happen) if the backlog keeps
            // being fed faster than it drains. Checking the data's own age
            // catches this directly instead of waiting for a connection
            // error that may arrive far too late.
            const auto age = std::chrono::system_clock::now() - quote.exchangeTimestamp;
            if (age > kStaleQuoteThreshold) {
                const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(age).count();
                std::cerr << "[QuoteFeedSubscriber] quote on '" << channel_ << "' is " << ageMs
                           << "ms old -- connection is almost certainly stuck on a stale backlog; "
                              "forcing a fresh reconnect\n";
                reconnectWithRetry(shouldStop);
                continue;
            }

            handler(quote);
        } catch (const std::exception& ex) {
            std::cerr << "[QuoteFeedSubscriber] dropping malformed message: " << ex.what() << "\n";
        }
    }
}
