#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
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
    // stop() notifies this instead of leaving the thread to ride out a full
    // sleep_for(interval_) — without it, stopping a timer with a long
    // interval (KuCoin's ping cadence is ~18s) blocks the caller for up to
    // that long.
    std::mutex mutex_;
    std::condition_variable cv_;
};
