#pragma once
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
namespace opennow {
// Device/service calls only happen on this worker, never in Read/Request.
// Destruction joins an outstanding query; the callback must outlive the sampler.
template<class Value> class BackgroundSampler {
public:
    struct Snapshot {
        Value value{};
        bool valid = false;
        std::chrono::steady_clock::time_point updated{};
    };
    BackgroundSampler(std::function<Value()> fetch, std::chrono::milliseconds interval)
        : fetch_(std::move(fetch)), interval_(interval), worker_([this] { Run(); }) {}
    ~BackgroundSampler() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
        wake_.notify_all();
        if(worker_.joinable()) worker_.join();
    }
    Snapshot Read() const {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        return lock.owns_lock() ? snapshot_ : Snapshot{};
    }
    void Request() {
        { std::lock_guard<std::mutex> lock(mutex_); requested_=true; }
        wake_.notify_all();
    }
    BackgroundSampler(const BackgroundSampler&)=delete;
    BackgroundSampler& operator=(const BackgroundSampler&)=delete;
private:
    void Run() {
        for(;;) {
            { std::lock_guard<std::mutex> lock(mutex_); if(stopping_) return; requested_=false; }
            try {
                auto value=fetch_(); // No snapshot mutex held during OS IPC.
                std::lock_guard<std::mutex> lock(mutex_);
                if(stopping_) return;
                snapshot_={std::move(value),true,std::chrono::steady_clock::now()};
            } catch(...) {} // Keep the last valid observation on service failure.
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock,interval_,[this] { return stopping_ || requested_; });
            if(stopping_) return;
        }
    }
    std::function<Value()> fetch_;
    std::chrono::milliseconds interval_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Snapshot snapshot_;
    bool stopping_=false, requested_=false;
    std::thread worker_;
};
}
