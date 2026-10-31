#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
namespace opennow::network {
// Shared by background settings probes and startup selection. Per-sweep worker
// limits alone do not bound concurrent sockets when both sweeps run together.
class ProbeBudget {
public:
    explicit ProbeBudget(unsigned capacity = 3) : capacity_(capacity) {}
    bool Acquire(std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!available_.wait_until(lock, deadline, [&] { return used_ < capacity_; })) return false;
        ++used_; return true;
    }
    void Release() {
        { std::lock_guard<std::mutex> lock(mutex_); if (used_) --used_; }
        available_.notify_one();
    }
private:
    const unsigned capacity_;
    unsigned used_ = 0;
    std::mutex mutex_;
    std::condition_variable available_;
};
class ProbeLease {
public:
    ProbeLease(ProbeBudget& budget, std::chrono::steady_clock::time_point deadline)
        : budget_(budget), acquired_(budget.Acquire(deadline)) {}
    ~ProbeLease() { if (acquired_) budget_.Release(); }
    explicit operator bool() const { return acquired_; }
    ProbeLease(const ProbeLease&) = delete;
    ProbeLease& operator=(const ProbeLease&) = delete;
private:
    ProbeBudget& budget_;
    bool acquired_;
};
}
