//
//  DiscoverManager.cpp
//  Moonlight
//
// Created by XITRIX on 31.03.2025.
//

#include "AVFrameHolder.hpp"
#include "stream/DecodeQueuePolicy.hpp"
#include "VideoFrameTiming.hpp"

#include "borealis.hpp"

#include <cstdlib>

AVFrameQueue::AVFrameQueue() {}

AVFrameQueue::~AVFrameQueue() {
    cleanup();
}

size_t AVFrameQueue::discardFrom(uint32_t timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t removed=0;
    // Only not-yet-selected pictures are removed. Keep the displayed buffer
    // and its references alive so recovery can repeat the last good picture.
    for(size_t i=queue.size();i>0;--i) {
        const auto index=i-1;
        if(queue[index]->pts==AV_NOPTS_VALUE || opennow::video::RtpDelta(
                static_cast<uint32_t>(queue[index]->pts),timestamp)>=0) {
            headers_.Recycle(queue[index]);
            queue.erase(queue.begin()+index);
            generations.erase(generations.begin()+index);
            pushedAt.erase(pushedAt.begin()+index);
            ++removed;
        }
    }
    framesDroppedStat+=removed;
    return removed;
}

void AVFrameQueue::push(const AVFrame* item) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        AVFrame* copy = headers_.Reference(item);
        if (!copy) {
            brls::Logger::warning("Frame queue: failed to reference decoded frame");
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (lastPushAt.time_since_epoch().count() != 0) {
            const int64_t interval_us =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - lastPushAt).count();
            if (interval_us > 35000)
                if (ready_latency_.Protect(static_cast<uint64_t>(std::chrono::duration_cast<
                    std::chrono::microseconds>(now.time_since_epoch()).count()))) ++ready_metrics_.latency_backoffs;
            // Ignore pauses and duplicate burst completions. The EWMA follows
            // real 30/60 FPS cadence without reacting to a single late frame.
            if (interval_us >= 5000 && interval_us <= 50000) {
                if (observedProducerIntervalUs == 0) {
                    observedProducerIntervalUs = interval_us;
                } else {
                    const int64_t deviation_us =
                        std::llabs(interval_us - observedProducerIntervalUs);
                    observedProducerJitterUs =
                        observedProducerJitterUs == 0
                            ? deviation_us
                            : (observedProducerJitterUs * 7 + deviation_us) / 8;
                    observedProducerIntervalUs =
                        (observedProducerIntervalUs * 7 + interval_us) / 8;
                }
            }
        }
        lastPushAt = now;
        consecutiveWaitTimeouts = 0;
        if (item->pts != AV_NOPTS_VALUE) {
            const uint32_t timestamp = static_cast<uint32_t>(item->pts);
            if (hasLastPushedRtpTimestamp) {
                const int32_t interval_ticks =
                    opennow::video::RtpDelta(timestamp, lastPushedRtpTimestamp);
                // RTP video uses a 90 kHz clock. Ignore duplicates, decode
                // reordering and long discontinuities while following genuine
                // 30-60 FPS source cadence and short server-side frame gaps.
                if (interval_ticks >= 300 && interval_ticks <= 9000) {
                    observedProducerRtpTicks =
                        observedProducerRtpTicks == 0
                            ? interval_ticks
                            : (observedProducerRtpTicks * 7 + interval_ticks) / 8;
                }
            }
            lastPushedRtpTimestamp = timestamp;
            hasLastPushedRtpTimestamp = true;
        }
        queue.push_back(copy);
        generations.push_back(++nextGeneration);
        pushedAt.push_back(now);
        framesCopiedStat++;

        if (queue.size() > limit) {
            AVFrame* dropped = queue.front();
            queue.pop_front();
            generations.pop_front();
            pushedAt.pop_front();
            headers_.Recycle(dropped);
            framesDroppedStat++;
            overflowDroppedStat++;
            if (ready_latency_.Protect(static_cast<uint64_t>(std::chrono::duration_cast<
                std::chrono::microseconds>(now.time_since_epoch()).count()))) ++ready_metrics_.latency_backoffs;
        }
    }
    m_frame_available.notify_one();
}

AVFrame* AVFrameQueue::pop(bool& reused, uint64_t& generation, int64_t target_pts) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (!presentationStarted && bufferFrame == nullptr &&
        queue.size() < startupDepth) {
        const auto startup_wait = std::min(
            std::chrono::microseconds(5000), emptyWaitBudget * 2);
        briefWaitStat++;
        if (m_frame_available.wait_for(lock, startup_wait, [this] {
                return queue.size() >= startupDepth;
            })) {
            briefWaitRecoveredStat++;
        }
        if (queue.size() < startupDepth) {
            startupHoldStat++;
            reused = false;
            generation = 0;
            return nullptr;
        }
    }

    if (queue.empty() && bufferFrame != nullptr &&
        emptyWaitBudget.count() > 0) {
        const auto elapsed_us = lastPushAt.time_since_epoch().count() == 0 ? 0 :
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - lastPushAt).count();
        // Decoded frames arriving in bursts bias wall-clock intervals upward.
        // RTP cadence remains the better predictor while it is in normal range.
        const int64_t cadence_us = observedProducerRtpTicks >= 450 && observedProducerRtpTicks <= 4500
            ? static_cast<int64_t>(observedProducerRtpTicks) * 1000000 / 90000
            : observedProducerIntervalUs;
        const auto predicted_wait = std::chrono::microseconds(opennow::video::PredictFrameWaitUs(
            emptyWaitBudget.count(), consecutiveWaitTimeouts, cadence_us,
            observedProducerJitterUs, elapsed_us));
        if (!predicted_wait.count()) predictiveWaitSkippedStat++;

        if (predicted_wait.count() > 0) {
            briefWaitStat++;
            const auto wait_started_at = std::chrono::steady_clock::now();
            const bool recovered = m_frame_available.wait_for(
                lock, predicted_wait, [this] {
                    return !queue.empty();
                });
            const uint64_t waited_us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - wait_started_at).count());
            briefWaitTotalUs += waited_us;
            briefWaitMaximumUs = std::max(briefWaitMaximumUs, waited_us);
            if (recovered) {
                briefWaitRecoveredStat++;
                consecutiveWaitTimeouts = 0;
            } else {
                briefWaitTimeoutStat++;
                ++consecutiveWaitTimeouts;
            }
        }
    }

    // Preserve normal FIFO delivery. Only discard a frame that has become very
    // old during a render/decode stall when a newer decoded frame is ready.
    // This prevents a multi-frame catch-up burst without recreating R4's
    // aggressive RTP-span dropping.
    const auto presentation_now = std::chrono::steady_clock::now();
    while (queue.size() > 1 && !pushedAt.empty() &&
           presentation_now - pushedAt.front() >=
               std::chrono::milliseconds(opennow::video::MaximumReadyFrameAgeMs())) {
        AVFrame* dropped = queue.front();
        queue.pop_front();
        generations.pop_front();
        pushedAt.pop_front();
        headers_.Recycle(dropped);
        framesDroppedStat++;
        staleAgeDroppedStat++;
        if (ready_latency_.Protect(static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::microseconds>(presentation_now.time_since_epoch()).count()))) ++ready_metrics_.latency_backoffs;
    }

    const bool external_clock_present = target_pts != AV_NOPTS_VALUE;
    if (external_clock_present) {
        while (!queue.empty() && queue.front()->pts != AV_NOPTS_VALUE) {
            const bool has_next = queue.size() > 1 && queue[1]->pts != AV_NOPTS_VALUE;
            const int32_t normal_tolerance =
                opennow::video::AdaptiveEarlyToleranceTicks(
                    observedProducerRtpTicks);
            pacingEarlyToleranceTicks =
                currentConsecutiveReuse > 0
                    ? opennow::video::RecoveryEarlyToleranceTicks(
                          observedProducerRtpTicks)
                    : normal_tolerance;
            const uint32_t front_timestamp =
                static_cast<uint32_t>(queue.front()->pts);
            lastPacingPhaseTicks = opennow::video::RtpDelta(
                front_timestamp, static_cast<uint32_t>(target_pts));
            const opennow::video::FrameTimingPolicy policy {
                pacingEarlyToleranceTicks
            };
            const auto decision = opennow::video::DecideFrame(
                front_timestamp, has_next,
                has_next ? static_cast<uint32_t>(queue[1]->pts) : 0,
                bufferFrame != nullptr,
                static_cast<uint32_t>(target_pts),
                policy);
            if (decision == opennow::video::FrameDecision::HoldPrevious) {
                fakeFrameUsedStat++;
                timingHoldStat++;
                if (ready_latency_.Protect(static_cast<uint64_t>(std::chrono::duration_cast<
                    std::chrono::microseconds>(presentation_now.time_since_epoch()).count()))) ++ready_metrics_.latency_backoffs;
                currentConsecutiveReuse++;
                maximumConsecutiveReuse =
                    std::max(maximumConsecutiveReuse, currentConsecutiveReuse);
                reused = true;
                generation = bufferGeneration;
                return bufferFrame;
            }
            if (decision != opennow::video::FrameDecision::DropSuperseded)
                break;
            AVFrame* dropped = queue.front();
            queue.pop_front();
            generations.pop_front();
            pushedAt.pop_front();
            headers_.Recycle(dropped);
            framesDroppedStat++;
            timingDroppedStat++;
        }
    } else {
        // RTP timestamps preserve ordering, but they are not presentation
        // deadlines until WebRTC supplies a correlated media clock.
        pacingEarlyToleranceTicks = 0;
        lastPacingPhaseTicks = 0;
    }

    const auto now_us = static_cast<uint64_t>(std::chrono::duration_cast<
        std::chrono::microseconds>(presentation_now.time_since_epoch()).count());
    if (!queue.empty() && presentationStarted && !pushedAt.empty()) {
        const auto age = [&](size_t i) {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                presentation_now - pushedAt[i]).count());
        };
        const bool next = queue.size() > 1 && queue[0]->pts != AV_NOPTS_VALUE &&
            queue[1]->pts != AV_NOPTS_VALUE;
        const int32_t step = next ? opennow::video::RtpDelta(
            static_cast<uint32_t>(queue[1]->pts), static_cast<uint32_t>(queue[0]->pts)) : 0;
        const int64_t period = static_cast<int64_t>(observedProducerRtpTicks) * 1000000 / 90000;
        if (ready_latency_.Consider(now_us, period, observedProducerJitterUs,
                observedProducerIntervalUs, queue.size(), age(0), next ? age(1) : 0,
                step, external_clock_present)) {
            auto* dropped = queue.front();
            queue.pop_front(); generations.pop_front(); pushedAt.pop_front();
            headers_.Recycle(dropped);
            ++framesDroppedStat;
            ++ready_metrics_.latency_trims;
        }
    }

    if (!queue.empty()) {
        if (bufferFrame)
            headers_.Recycle(bufferFrame);

        const auto selected_depth = static_cast<unsigned>(queue.size());
        AVFrame* item = queue.front();
        queue.pop_front();
        bufferGeneration = generations.front();
        generations.pop_front();
        bufferPushedAt = pushedAt.front();
        pushedAt.pop_front();
        bufferFrame = item;
        const uint64_t age_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - bufferPushedAt).count());
        ready_metrics_.Observe(age_us, selected_depth);
        presentationAgeCount++;
        presentationAgeTotalUs += age_us;
        presentationAgeMaximumUs = std::max(presentationAgeMaximumUs, age_us);
        presentationStarted = true;
        if (external_clock_present)
            sourceClockPresentStat++;
        else
            clockBypassPresentStat++;
        currentConsecutiveReuse = 0;
        reused = false;
    } else {
        fakeFrameUsedStat++;
        if (ready_latency_.Protect(now_us)) ++ready_metrics_.latency_backoffs;
        queueEmptyReuseStat++;
        currentConsecutiveReuse++;
        maximumConsecutiveReuse =
            std::max(maximumConsecutiveReuse, currentConsecutiveReuse);
        reused = true;
    }

    generation = bufferGeneration;
    return bufferFrame;
}

opennow::video::ReadyFrameMetrics AVFrameQueue::readyMetrics() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return ready_metrics_;
}

QueueFrameHeaders::Stats AVFrameQueue::headerStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return headers_.GetStats();
}

size_t AVFrameQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return queue.size();
}

size_t AVFrameQueue::getFakeFrameUsage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return fakeFrameUsedStat;
}

size_t AVFrameQueue::getFramesDropStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return framesDroppedStat;
}

size_t AVFrameQueue::getTimingDropStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return timingDroppedStat;
}

size_t AVFrameQueue::getOverflowDropStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return overflowDroppedStat;
}

size_t AVFrameQueue::getStaleAgeDropStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return staleAgeDroppedStat;
}

size_t AVFrameQueue::getTimingHoldStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return timingHoldStat;
}

size_t AVFrameQueue::getQueueEmptyReuseStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return queueEmptyReuseStat;
}

size_t AVFrameQueue::getBriefWaitStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return briefWaitStat;
}

size_t AVFrameQueue::getBriefWaitRecoveredStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return briefWaitRecoveredStat;
}

size_t AVFrameQueue::getBriefWaitTimeoutStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return briefWaitTimeoutStat;
}

size_t AVFrameQueue::getPredictiveWaitSkippedStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return predictiveWaitSkippedStat;
}

size_t AVFrameQueue::getStartupHoldStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return startupHoldStat;
}

uint64_t AVFrameQueue::getBriefWaitTotalUs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return briefWaitTotalUs;
}

uint64_t AVFrameQueue::getBriefWaitMaximumUs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return briefWaitMaximumUs;
}

int64_t AVFrameQueue::getObservedProducerIntervalUs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return observedProducerIntervalUs;
}

int64_t AVFrameQueue::getObservedProducerJitterUs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return observedProducerJitterUs;
}

int32_t AVFrameQueue::getObservedProducerRtpTicks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return observedProducerRtpTicks;
}

int32_t AVFrameQueue::getPacingEarlyToleranceTicks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return pacingEarlyToleranceTicks;
}

int32_t AVFrameQueue::getLastPacingPhaseTicks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return lastPacingPhaseTicks;
}

size_t AVFrameQueue::getCurrentConsecutiveReuse() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return currentConsecutiveReuse;
}

size_t AVFrameQueue::getMaximumConsecutiveReuse() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return maximumConsecutiveReuse;
}

size_t AVFrameQueue::getSourceClockPresentStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return sourceClockPresentStat;
}

size_t AVFrameQueue::getClockBypassPresentStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return clockBypassPresentStat;
}

uint64_t AVFrameQueue::getPresentationAgeCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return presentationAgeCount;
}

uint64_t AVFrameQueue::getPresentationAgeTotalUs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return presentationAgeTotalUs;
}

uint64_t AVFrameQueue::getPresentationAgeMaximumUs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return presentationAgeMaximumUs;
}

void AVFrameQueue::cleanup() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!queue.empty()) {
        AVFrame* frame = queue.front();
        queue.pop_front();
        headers_.Recycle(frame);
    }
    while (!generations.empty())
        generations.pop_front();
    while (!pushedAt.empty())
        pushedAt.pop_front();

    if (bufferFrame)
        headers_.Recycle(bufferFrame);

    headers_.Clear();
    ready_metrics_ = {};
    ready_latency_ = {};
    fakeFrameUsedStat = 0;
    framesDroppedStat = 0;
    timingDroppedStat = 0;
    overflowDroppedStat = 0;
    staleAgeDroppedStat = 0;
    timingHoldStat = 0;
    queueEmptyReuseStat = 0;
    briefWaitStat = 0;
    briefWaitRecoveredStat = 0;
    briefWaitTimeoutStat = 0;
    predictiveWaitSkippedStat = 0;
    startupHoldStat = 0;
    briefWaitTotalUs = 0;
    briefWaitMaximumUs = 0;
    framesCopiedStat = 0;
    nextGeneration = 0;
    bufferGeneration = 0;
    bufferFrame = nullptr;
    presentationStarted = false;
    lastPushAt = {};
    observedProducerIntervalUs = 0;
    observedProducerJitterUs = 0;
    hasLastPushedRtpTimestamp = false;
    lastPushedRtpTimestamp = 0;
    observedProducerRtpTicks = 0;
    pacingEarlyToleranceTicks = 180;
    lastPacingPhaseTicks = 0;
    currentConsecutiveReuse = 0;
    maximumConsecutiveReuse = 0;
    sourceClockPresentStat = 0;
    clockBypassPresentStat = 0;
    bufferPushedAt = {};
    presentationAgeCount = 0;
    presentationAgeTotalUs = 0;
    presentationAgeMaximumUs = 0;
}
