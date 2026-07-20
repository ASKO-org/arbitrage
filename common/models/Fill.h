#pragma once
#include <chrono>
#include <string>

// One fill (full or partial) against an order, persisted to the fills table
// by DatabaseRepository::insertFill().
struct Fill {
    std::string orderId;
    double qty = 0.0;
    double price = 0.0;
    double fee = 0.0;
    std::chrono::system_clock::time_point executedAt;
};
