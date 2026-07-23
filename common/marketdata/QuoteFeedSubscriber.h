#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "marketdata/Quote.h"

struct redisContext;

// Consumes quotes from a Redis pub/sub channel published by
// QuoteFeedPublisher (market_data_feed). Pub/sub, not a Streams consumer
// group, is deliberate here: an occasional missed quote under a brief
// subscriber restart is harmless (a fresher one follows immediately), so
// there's no need for the durability/ack/replay machinery OrderIntake uses
// for order flow.
class QuoteFeedSubscriber {
public:
    // Throws std::runtime_error if the connection to Redis fails.
    QuoteFeedSubscriber(const std::string& host, int port, std::string channel);
    ~QuoteFeedSubscriber();

    // Blocks, invoking handler(quote) for each message received. Wakes at
    // least once per second (socket receive timeout) to recheck shouldStop(),
    // so it returns promptly after shouldStop() starts returning true.
    void run(const std::function<void(const Quote&)>& handler, const std::function<bool()>& shouldStop);

private:
    struct RedisContextDeleter {
        void operator()(redisContext* context) const;
    };

    // (Re)connects and re-issues SUBSCRIBE. Throws std::runtime_error if the
    // connection fails.
    void connect();

    // Retries connect() once per second until it succeeds or shouldStop()
    // returns true.
    void reconnectWithRetry(const std::function<bool()>& shouldStop);

    std::string host_;
    int port_;
    std::string channel_;
    std::unique_ptr<redisContext, RedisContextDeleter> context_;

    // Updated every time a genuine message is received (regardless of its
    // own staleness). Lets run() detect a connection that has gone
    // completely silent — not even stale backlog left to check the age of
    // — which the per-message staleness check can't see at all.
    std::chrono::steady_clock::time_point lastMessageAt_;
};
