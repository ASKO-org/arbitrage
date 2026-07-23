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
//
// A pub/sub client that falls behind gets disconnected by Redis itself
// (its client-output-buffer limit), but hiredis may still have a backlog
// buffered locally from before the drop — so a dead connection doesn't
// always announce itself as a read error. run() treats three independent
// signals as equally conclusive proof the connection needs replacing,
// each catching a case the others can't see:
//   1. A real read error (redisGetReply fails with something other than
//      a timeout) — the ordinary, expected case.
//   2. A message arrives whose own timestamp is already stale — proof
//      we're draining old backlog, not live data.
//   3. Total silence for too long — proof the connection is dead even
//      when there's no backlog left to check the age of (case 2 can't
//      detect this at all).
// All three funnel into the same reconnectDueTo() -> reconnectWithRetry()
// path, which also enforces a cooldown so a recurring condition can't
// spin faster than ~1 reconnect/sec.
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

    // Logs reason (already describing which of the three signals fired
    // and on which channel) and reconnects. The single funnel point for
    // all three dead-connection signals described above.
    void reconnectDueTo(const std::string& reason, const std::function<bool()>& shouldStop);

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
