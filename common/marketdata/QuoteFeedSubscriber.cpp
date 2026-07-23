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

// If a channel that's normally active goes this long without delivering
// *anything* — not even a stale message to check the age of — that's
// equally proof the connection is dead. This catches the case the
// per-message staleness check can't see at all: a connection that died
// with no backlog left to drain, which just times out silently forever
// otherwise (the original bug, for a case the staleness check doesn't
// cover). Slightly more generous than kStaleQuoteThreshold to avoid
// overlapping false-positives right after a fresh reconnect.
constexpr auto kNoMessageTimeout = std::chrono::seconds(20);
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

    // Reset the silence clock on every (re)connect so a fresh connection
    // gets a full kNoMessageTimeout window to receive its first message,
    // rather than inheriting whatever was left of the previous one.
    lastMessageAt_ = std::chrono::steady_clock::now();
}

void QuoteFeedSubscriber::reconnectWithRetry(const std::function<bool()>& shouldStop) {
    while (!shouldStop()) {
        try {
            connect();
            std::cerr << "[QuoteFeedSubscriber] reconnected on '" << channel_ << "'\n";
            // Cooldown even on success. Without this, a *successful* reconnect
            // went straight back into the read loop with zero delay — if the
            // same staleness condition recurs immediately (e.g. an upstream
            // publisher persistently running behind, not a one-off drop),
            // that's an unbounded reconnect loop with no brake anywhere in
            // it. Measured: 45,832 reconnects in ~60s (843% CPU) before this
            // fix. A 1s floor caps every subscriber at ~1 reconnect/sec no
            // matter what's triggering it, turning a resource-exhausting
            // storm into a slow, visible, still-diagnosable symptom.
            std::this_thread::sleep_for(std::chrono::seconds(1));
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
                // Receive timed out with nothing new — normal, happens every
                // ~1s. But if this channel has gone suspiciously long
                // without delivering anything at all, that's the "dead
                // connection with no backlog left" case the per-message
                // staleness check below can't see.
                const auto silence = std::chrono::steady_clock::now() - lastMessageAt_;
                if (silence > kNoMessageTimeout) {
                    const auto silenceMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(silence).count();
                    std::cerr << "[QuoteFeedSubscriber] no message at all on '" << channel_ << "' for "
                               << silenceMs << "ms -- connection is almost certainly dead; forcing a "
                                              "fresh reconnect\n";
                    reconnectWithRetry(shouldStop);
                }
                continue;  // recheck shouldStop()
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

        lastMessageAt_ = std::chrono::steady_clock::now();

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
