#pragma once

#include "Singleton.hpp"
#include "../FrameHeaderPool.hpp"
#include "../ReadyFrameMetrics.hpp"
#include "../ReadyFrameLatencyPolicy.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <deque>
#include <algorithm>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
}

struct AVFrame;
struct QueueFrameOps {
    static AVFrame* Allocate() { return av_frame_alloc(); }
    static int Reference(AVFrame* out,const AVFrame* source) { return av_frame_ref(out,source); }
    static void Unref(AVFrame* frame) { av_frame_unref(frame); }
    static void Free(AVFrame** frame) { av_frame_free(frame); }
};
using QueueFrameHeaders = opennow::video::FrameHeaderPool<AVFrame,QueueFrameOps>;

class AVFrameQueue {
public:
    explicit AVFrameQueue();
    ~AVFrameQueue();

    void push(const AVFrame* item);
    size_t discardFrom(uint32_t timestamp);
    AVFrame* pop(bool& reused, uint64_t& generation, int64_t target_pts = AV_NOPTS_VALUE);

    [[nodiscard]] size_t size() const;
    QueueFrameHeaders::Stats headerStats() const;
    opennow::video::ReadyFrameMetrics readyMetrics() const;
    [[nodiscard]] size_t getFakeFrameUsage() const;
    [[nodiscard]] size_t getFramesDropStat() const;
    [[nodiscard]] size_t getTimingDropStat() const;
    [[nodiscard]] size_t getOverflowDropStat() const;
    [[nodiscard]] size_t getStaleAgeDropStat() const;
    [[nodiscard]] size_t getTimingHoldStat() const;
    [[nodiscard]] size_t getQueueEmptyReuseStat() const;
    [[nodiscard]] size_t getBriefWaitStat() const;
    [[nodiscard]] size_t getBriefWaitRecoveredStat() const;
    [[nodiscard]] size_t getBriefWaitTimeoutStat() const;
    [[nodiscard]] size_t getPredictiveWaitSkippedStat() const;
    [[nodiscard]] size_t getStartupHoldStat() const;
    [[nodiscard]] uint64_t getBriefWaitTotalUs() const;
    [[nodiscard]] uint64_t getBriefWaitMaximumUs() const;
    [[nodiscard]] int64_t getObservedProducerIntervalUs() const;
    [[nodiscard]] int64_t getObservedProducerJitterUs() const;
    [[nodiscard]] int32_t getObservedProducerRtpTicks() const;
    [[nodiscard]] int32_t getPacingEarlyToleranceTicks() const;
    [[nodiscard]] int32_t getLastPacingPhaseTicks() const;
    [[nodiscard]] size_t getCurrentConsecutiveReuse() const;
    [[nodiscard]] size_t getMaximumConsecutiveReuse() const;
    [[nodiscard]] size_t getSourceClockPresentStat() const;
    [[nodiscard]] size_t getClockBypassPresentStat() const;
    [[nodiscard]] uint64_t getPresentationAgeCount() const;
    [[nodiscard]] uint64_t getPresentationAgeTotalUs() const;
    [[nodiscard]] uint64_t getPresentationAgeMaximumUs() const;

    void cleanup();

private:
    friend class AVFrameHolder;
    size_t limit = 3;
    QueueFrameHeaders headers_;
    opennow::video::ReadyFrameMetrics ready_metrics_;
    opennow::video::ReadyFrameLatencyPolicy ready_latency_;
    std::deque<AVFrame*> queue;
    AVFrame* bufferFrame = nullptr;
    mutable std::mutex m_mutex;
    std::condition_variable m_frame_available;
    size_t fakeFrameUsedStat = 0;
    size_t framesDroppedStat = 0;
    size_t timingDroppedStat = 0;
    size_t overflowDroppedStat = 0;
    size_t staleAgeDroppedStat = 0;
    size_t timingHoldStat = 0;
    size_t queueEmptyReuseStat = 0;
    size_t briefWaitStat = 0;
    size_t briefWaitRecoveredStat = 0;
    size_t briefWaitTimeoutStat = 0;
    size_t predictiveWaitSkippedStat = 0;
    unsigned consecutiveWaitTimeouts = 0;
    size_t startupHoldStat = 0;
    uint64_t briefWaitTotalUs = 0;
    uint64_t briefWaitMaximumUs = 0;
    size_t framesCopiedStat = 0;
    uint64_t nextGeneration = 0;
    uint64_t bufferGeneration = 0;
    std::deque<uint64_t> generations;
    std::deque<std::chrono::steady_clock::time_point> pushedAt;
    std::chrono::steady_clock::time_point bufferPushedAt {};
    size_t startupDepth = 2;
    bool presentationStarted = false;
    std::chrono::microseconds emptyWaitBudget {2500};
    std::chrono::steady_clock::time_point lastPushAt {};
    int64_t observedProducerIntervalUs = 0;
    int64_t observedProducerJitterUs = 0;
    bool hasLastPushedRtpTimestamp = false;
    uint32_t lastPushedRtpTimestamp = 0;
    int32_t observedProducerRtpTicks = 0;
    int32_t pacingEarlyToleranceTicks = 180;
    int32_t lastPacingPhaseTicks = 0;
    size_t currentConsecutiveReuse = 0;
    size_t maximumConsecutiveReuse = 0;
    size_t sourceClockPresentStat = 0;
    size_t clockBypassPresentStat = 0;
    uint64_t presentationAgeCount = 0;
    uint64_t presentationAgeTotalUs = 0;
    uint64_t presentationAgeMaximumUs = 0;
};

class AVFrameHolder : public Singleton<AVFrameHolder> {
  public:
    void push(const AVFrame* frame) {
        m_frame_queue.push(frame);
    }
    size_t discardFrom(uint32_t timestamp) { return m_frame_queue.discardFrom(timestamp); }

    void get(const std::function<void(AVFrame*, uint64_t, bool)>& fn,
             int64_t target_pts = AV_NOPTS_VALUE) {
        bool reused = false;
        uint64_t generation = 0;
        auto frame = m_frame_queue.pop(reused, generation, target_pts);

        if (frame) {
            fn(frame, generation, reused);
        }
    }

    void setWaitBudget(int microseconds) {
        std::lock_guard<std::mutex> lock(m_frame_queue.m_mutex);
        m_frame_queue.emptyWaitBudget = std::chrono::microseconds(
            std::clamp(microseconds, 0, 6500));
    }

    void prepare(size_t queue_limit = 3, size_t startup_depth = 2,
                 std::chrono::microseconds empty_wait_budget =
                     std::chrono::microseconds(2500)) {
        // Keep a small capacity for decode/RTX bursts. Only startup requires
        // two ready pictures; steady state waits briefly rather than carrying
        // a permanent extra frame of latency.
        m_frame_queue.limit = std::max<size_t>(2, queue_limit);
        m_frame_queue.startupDepth =
            std::clamp<size_t>(startup_depth, 1, m_frame_queue.limit);
        m_frame_queue.emptyWaitBudget =
            std::clamp(empty_wait_budget, std::chrono::microseconds(0),
                       std::chrono::microseconds(6500));
    }

    void cleanup() {
        m_frame_queue.cleanup();
    }

    [[nodiscard]] int getStat() const { return static_cast<int>(m_frame_queue.size()); }
    auto getReadyMetrics() const { return m_frame_queue.readyMetrics(); }
    auto getHeaderPoolStats() const { return m_frame_queue.headerStats(); }
    [[nodiscard]] size_t getFakeFrameStat() const { return m_frame_queue.getFakeFrameUsage(); }
    [[nodiscard]] size_t getFrameDropStat() const { return m_frame_queue.getFramesDropStat(); }
    [[nodiscard]] size_t getTimingDropStat() const { return m_frame_queue.getTimingDropStat(); }
    [[nodiscard]] size_t getOverflowDropStat() const { return m_frame_queue.getOverflowDropStat(); }
    [[nodiscard]] size_t getStaleAgeDropStat() const { return m_frame_queue.getStaleAgeDropStat(); }
    [[nodiscard]] size_t getTimingHoldStat() const { return m_frame_queue.getTimingHoldStat(); }
    [[nodiscard]] size_t getQueueEmptyReuseStat() const { return m_frame_queue.getQueueEmptyReuseStat(); }
    [[nodiscard]] size_t getBriefWaitStat() const { return m_frame_queue.getBriefWaitStat(); }
    [[nodiscard]] size_t getBriefWaitRecoveredStat() const { return m_frame_queue.getBriefWaitRecoveredStat(); }
    [[nodiscard]] size_t getBriefWaitTimeoutStat() const { return m_frame_queue.getBriefWaitTimeoutStat(); }
    [[nodiscard]] size_t getPredictiveWaitSkippedStat() const { return m_frame_queue.getPredictiveWaitSkippedStat(); }
    [[nodiscard]] size_t getStartupHoldStat() const { return m_frame_queue.getStartupHoldStat(); }
    [[nodiscard]] uint64_t getBriefWaitTotalUs() const { return m_frame_queue.getBriefWaitTotalUs(); }
    [[nodiscard]] uint64_t getBriefWaitMaximumUs() const { return m_frame_queue.getBriefWaitMaximumUs(); }
    [[nodiscard]] int64_t getObservedProducerIntervalUs() const { return m_frame_queue.getObservedProducerIntervalUs(); }
    [[nodiscard]] int64_t getObservedProducerJitterUs() const { return m_frame_queue.getObservedProducerJitterUs(); }
    [[nodiscard]] int32_t getObservedProducerRtpTicks() const { return m_frame_queue.getObservedProducerRtpTicks(); }
    [[nodiscard]] int32_t getPacingEarlyToleranceTicks() const { return m_frame_queue.getPacingEarlyToleranceTicks(); }
    [[nodiscard]] int32_t getLastPacingPhaseTicks() const { return m_frame_queue.getLastPacingPhaseTicks(); }
    [[nodiscard]] size_t getCurrentConsecutiveReuse() const { return m_frame_queue.getCurrentConsecutiveReuse(); }
    [[nodiscard]] size_t getMaximumConsecutiveReuse() const { return m_frame_queue.getMaximumConsecutiveReuse(); }
    [[nodiscard]] size_t getSourceClockPresentStat() const { return m_frame_queue.getSourceClockPresentStat(); }
    [[nodiscard]] size_t getClockBypassPresentStat() const { return m_frame_queue.getClockBypassPresentStat(); }
    [[nodiscard]] uint64_t getPresentationAgeCount() const { return m_frame_queue.getPresentationAgeCount(); }
    [[nodiscard]] uint64_t getPresentationAgeTotalUs() const { return m_frame_queue.getPresentationAgeTotalUs(); }
    [[nodiscard]] uint64_t getPresentationAgeMaximumUs() const { return m_frame_queue.getPresentationAgeMaximumUs(); }
    [[nodiscard]] size_t getFrameQueueSize() const { return m_frame_queue.size(); }

  private:
    AVFrameQueue m_frame_queue;
};
