#pragma once
#include <chrono>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

enum class OrderStatus { Acked, Rejected, Filled, PartiallyFilled, Cancelled };

inline std::string toString(OrderStatus status) {
    switch (status) {
        case OrderStatus::Acked: return "acked";
        case OrderStatus::Rejected: return "rejected";
        case OrderStatus::Filled: return "filled";
        case OrderStatus::PartiallyFilled: return "partially_filled";
        case OrderStatus::Cancelled: return "cancelled";
    }
    throw std::runtime_error("OrderStatus: unhandled enum value");
}

inline OrderStatus orderStatusFromString(const std::string& value) {
    if (value == "acked") return OrderStatus::Acked;
    if (value == "rejected") return OrderStatus::Rejected;
    if (value == "filled") return OrderStatus::Filled;
    if (value == "partially_filled") return OrderStatus::PartiallyFilled;
    if (value == "cancelled") return OrderStatus::Cancelled;
    throw std::runtime_error("OrderStatus: unknown status '" + value + "'");
}

// Published to execution:reports for every order outcome — the durable,
// replayable audit trail a future PnL service reads independently of
// execution_service's own consumer group on execution:orders.
struct ExecutionReport {
    std::string orderId;
    std::string strategyId;
    std::string venue;
    std::string symbol;
    OrderStatus status = OrderStatus::Acked;
    double filledQty = 0.0;
    double avgPrice = 0.0;
    std::string errorMessage;  // set when status == Rejected
    std::chrono::system_clock::time_point reportedAt;
};

inline nlohmann::json toJson(const ExecutionReport& report) {
    const auto reportedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   report.reportedAt.time_since_epoch())
                                   .count();
    return nlohmann::json{
        {"order_id", report.orderId},       {"strategy_id", report.strategyId},
        {"venue", report.venue},            {"symbol", report.symbol},
        {"status", toString(report.status)}, {"filled_qty", report.filledQty},
        {"avg_price", report.avgPrice},      {"error_message", report.errorMessage},
        {"reported_at_ms", reportedAtMs},
    };
}
