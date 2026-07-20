#include "core/OrderIntake.h"

#include <iostream>
#include <stdexcept>

#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

namespace {

Order parseEntryFields(redisReply* fields) {
    std::string payload;
    for (size_t i = 0; i + 1 < fields->elements; i += 2) {
        const std::string field = fields->element[i]->str ? fields->element[i]->str : "";
        if (field == "payload") {
            payload = fields->element[i + 1]->str ? fields->element[i + 1]->str : "";
        }
    }
    return orderFromJson(nlohmann::json::parse(payload));
}

// Runs handler for every (entryId, fields) pair in a XREADGROUP/XAUTOCLAIM
// entries array, acking each one handler doesn't throw on.
void processEntries(redisReply* entries, const std::function<void(const Order&)>& handler,
                     const std::function<void(const std::string&)>& ack) {
    for (size_t i = 0; i < entries->elements; ++i) {
        redisReply* entry = entries->element[i];
        if (entry->type != REDIS_REPLY_ARRAY || entry->elements < 2 || !entry->element[1]) {
            continue;  // XAUTOCLAIM can report a since-deleted entry id with a null field array
        }
        const std::string entryId = entry->element[0]->str ? entry->element[0]->str : "";
        try {
            const Order order = parseEntryFields(entry->element[1]);
            handler(order);
            ack(entryId);
        } catch (const std::exception& ex) {
            std::cerr << "[OrderIntake] handler failed for entry " << entryId << ": " << ex.what()
                      << " (left unacked, will be reclaimed)\n";
        }
    }
}

}  // namespace

void OrderIntake::RedisContextDeleter::operator()(redisContext* context) const {
    if (context) redisFree(context);
}

OrderIntake::OrderIntake(const std::string& host, int port, std::string stream, std::string group,
                          std::string consumerName, std::chrono::milliseconds minIdleTimeForReclaim)
    : stream_(std::move(stream)),
      group_(std::move(group)),
      consumerName_(std::move(consumerName)),
      minIdleTimeForReclaim_(minIdleTimeForReclaim),
      context_(redisConnect(host.c_str(), port)) {
    if (!context_) {
        throw std::runtime_error("OrderIntake: redisConnect returned null (out of memory?)");
    }
    if (context_->err) {
        throw std::runtime_error("OrderIntake: failed to connect to Redis at " + host + ":" +
                                  std::to_string(port) + ": " + context_->errstr);
    }
}

OrderIntake::~OrderIntake() = default;

void OrderIntake::ensureGroup() {
    auto* reply = static_cast<redisReply*>(
        redisCommand(context_.get(), "XGROUP CREATE %s %s $ MKSTREAM", stream_.c_str(), group_.c_str()));
    if (!reply) {
        throw std::runtime_error("OrderIntake: XGROUP CREATE failed: " + std::string(context_->errstr));
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        const std::string message = reply->str ? reply->str : "";
        freeReplyObject(reply);
        // BUSYGROUP just means the group already exists from a prior run.
        if (message.find("BUSYGROUP") == std::string::npos) {
            throw std::runtime_error("OrderIntake: XGROUP CREATE rejected: " + message);
        }
        return;
    }
    freeReplyObject(reply);
}

void OrderIntake::ack(const std::string& entryId) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(context_.get(), "XACK %s %s %s", stream_.c_str(), group_.c_str(), entryId.c_str()));
    if (reply) freeReplyObject(reply);
}

void OrderIntake::reclaimStalePending(const std::function<void(const Order&)>& handler) {
    auto* reply = static_cast<redisReply*>(redisCommand(
        context_.get(), "XAUTOCLAIM %s %s %s %lld 0-0 COUNT 10", stream_.c_str(), group_.c_str(),
        consumerName_.c_str(), static_cast<long long>(minIdleTimeForReclaim_.count())));
    if (!reply) return;  // best-effort; try again next loop iteration

    if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 2 && reply->element[1] &&
        reply->element[1]->type == REDIS_REPLY_ARRAY) {
        processEntries(reply->element[1], handler, [this](const std::string& id) { ack(id); });
    }
    freeReplyObject(reply);
}

void OrderIntake::run(const std::function<void(const Order&)>& handler,
                       const std::function<bool()>& shouldStop) {
    while (!shouldStop()) {
        reclaimStalePending(handler);
        if (shouldStop()) break;

        auto* reply = static_cast<redisReply*>(
            redisCommand(context_.get(), "XREADGROUP GROUP %s %s COUNT 10 BLOCK 2000 STREAMS %s >",
                         group_.c_str(), consumerName_.c_str(), stream_.c_str()));
        if (!reply) {
            throw std::runtime_error("OrderIntake: XREADGROUP failed: " + std::string(context_->errstr));
        }
        if (reply->type != REDIS_REPLY_ARRAY || reply->elements == 0) {
            freeReplyObject(reply);
            continue;  // BLOCK timed out with nothing new
        }

        // reply->element[0] is [stream_name, [entries...]]
        redisReply* streamEntry = reply->element[0];
        if (streamEntry->type == REDIS_REPLY_ARRAY && streamEntry->elements >= 2) {
            processEntries(streamEntry->element[1], handler, [this](const std::string& id) { ack(id); });
        }
        freeReplyObject(reply);
    }
}
