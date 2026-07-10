#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

// Invokes a callback on a dedicated thread at a fixed interval, until stopped.
class HeartbeatTimer {
public:
    HeartbeatTimer(std::chrono::milliseconds interval, std::function<void()> onTick);
    ~HeartbeatTimer();

    void start();
    void stop();

private:
    std::chrono::milliseconds interval_;
    std::function<void()> onTick_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
