#include "marketdata/HeartbeatTimer.h"

HeartbeatTimer::HeartbeatTimer(std::chrono::milliseconds interval, std::function<void()> onTick)
    : interval_(interval), onTick_(std::move(onTick)) {}

HeartbeatTimer::~HeartbeatTimer() { stop(); }

void HeartbeatTimer::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] {
        while (running_) {
            std::this_thread::sleep_for(interval_);
            if (running_) onTick_();
        }
    });
}

void HeartbeatTimer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}
