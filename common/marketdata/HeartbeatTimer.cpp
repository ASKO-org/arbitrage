#include "marketdata/HeartbeatTimer.h"

HeartbeatTimer::HeartbeatTimer(std::chrono::milliseconds interval, std::function<void()> onTick)
    : interval_(interval), onTick_(std::move(onTick)) {}

HeartbeatTimer::~HeartbeatTimer() { stop(); }

void HeartbeatTimer::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] {
        while (running_) {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool stoppedEarly =
                cv_.wait_for(lock, interval_, [this] { return !running_.load(); });
            lock.unlock();
            if (stoppedEarly) break;
            if (running_) onTick_();
        }
    });
}

void HeartbeatTimer::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}
