#include "webrtc_session.hpp"
#include "stream/audio/AudioRtpUtils.hpp"
#include "stream/DecodeQueuePolicy.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "stream/ffmpeg/VideoFrameTiming.hpp"
#include "internal.hpp"

#include <jansson.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

using namespace opennow::webrtc::internal;


void WebRtcSession::start_decoder_worker() {
    if (decoder_running_.exchange(true))
        return;

    video_frame_gate_.SetObserver([this](const opennow::video::DecodedFrameGate::Event& event) {
        // Invoked under the publication gate, sometimes from a queue path.
        // Never acquire peer/decoder-queue locks or reset the decoder here.
        if(event.fault) {
            begin_video_recovery();
            keyframe_needed_.store(true,std::memory_order_release);
            AppendStreamLog("VIDEO guard fault generation=" + std::to_string(event.generation) +
                " timestampKnown=" + std::to_string(event.known ? 1 : 0) +
                " timestamp=" + std::to_string(event.timestamp));
            return;
        }
        last_decoded_idr_timestamp_.store(event.timestamp,std::memory_order_release);
        has_decoded_idr_.store(true,std::memory_order_release);
        const auto recovery_start=recovery_started_us_.exchange(0);
        if(recovery_start) {
            const auto elapsed=NowUs()-recovery_start;
            AtomicMax(recovery_duration_us_max_,elapsed);
            recovery_duration_us_total_.fetch_add(elapsed);
            recovery_duration_count_.fetch_add(1);
            AppendStreamLog("VIDEO recovery_idr verifiedOutput=1 timestamp=" + std::to_string(event.timestamp) +
                " generation=" + std::to_string(event.generation) +
                " elapsedMs=" + std::to_string(elapsed/1000) +
                " pliRetries=" + std::to_string(recovery_pli_retries_.load()));
        }
        recovery_pli_retries_.store(0);
        // Do not clear keyframe_needed_ outside the gate: a newer fault could
        // otherwise lose its coalesced request. The transport consumes it.
    });
    if(decoder_) decoder_->set_frame_gate(&video_frame_gate_);
    decoder_thread_ = std::thread(&WebRtcSession::decoder_loop, this);
    AppendStreamLog("DECODE worker_started");
}

void WebRtcSession::decoder_loop() {
    for (;;) {
        DecodeUnit unit;
        {
            std::unique_lock<std::mutex> lock(decoder_queue_mutex_);
            decoder_queue_cv_.wait(lock, [this] {
                return !decoder_running_.load(std::memory_order_acquire) || !decoder_queue_.empty();
            });
            if (!decoder_running_.load(std::memory_order_acquire))
                break;

            unit = std::move(decoder_queue_.front());
            decoder_queue_.pop_front();
        }

        // Recheck even queued/in-flight generations after a newly observed
        // loss. Healthy pictures before the first unsafe timestamp survive.
        if(!video_frame_gate_.Begin(unit.rtp_timestamp,unit.idr)) {
            decoder_queue_drops_++;
            decoder_resync_drops_.fetch_add(1,std::memory_order_relaxed);
            std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
            recycle_decode_buffer_locked(std::move(unit.data));
            continue;
        }
        const auto decode_started_at = std::chrono::steady_clock::now();
        const uint64_t queue_wait_us = unit.enqueued_at.time_since_epoch().count() == 0 ? 0 :
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                decode_started_at - unit.enqueued_at).count());
        RecordLatency(queue_wait_buckets_, queue_wait_us);
        AtomicMax(queue_wait_us_max_, queue_wait_us);

        if (decoder_ && opennow::video::ConsumeResetForIdr(unit.idr,
                decoder_reset_requested_,unit.reset_decoder_before_decode))
            decoder_->reset_stream();

        const int decoded = decoder_
            ? decoder_->submit_decode_unit(unit.data.data(), static_cast<int>(unit.data.size()), unit.rtp_timestamp)
            : -1;
        const uint64_t decode_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - decode_started_at).count());
        decode_us_total_.fetch_add(decode_us, std::memory_order_relaxed);
        RecordLatency(decode_latency_buckets_, decode_us);
        AtomicMax(decode_us_max_, decode_us);

        const int access_unit_count = packets_received_.load();
        if (decoded > 0) {
            last_decoded_frame_at_us_.store(NowUs(), std::memory_order_release);
            const int frame_count = frames_decoded_.fetch_add(decoded) + decoded;
            if (!first_decoded_frame_logged_) {
                first_decoded_frame_logged_ = true;
                AppendStreamLog("DECODE first_frame frames=" + std::to_string(frame_count) +
                                " accessUnits=" + std::to_string(access_unit_count) +
                                " decoderDiag=" +
                                (decoder_ ? decoder_->setup_diagnostics() :
                                            std::string("decoder-unavailable")));
            } else if (frame_count - last_logged_frame_count_.load(std::memory_order_relaxed) >= 120) {
                AppendStreamLog("DECODE frame_progress frames=" + std::to_string(frame_count) +
                                " accessUnits=" + std::to_string(access_unit_count));
                last_logged_frame_count_.store(frame_count, std::memory_order_relaxed);
            }
        } else if (decoded < 0) {
            const int error_count = decode_errors_.fetch_add(1) + 1;
            if (error_count <= 5 ||
                error_count - last_logged_decode_error_count_.load(std::memory_order_relaxed) >= 20) {
                AppendStreamLog("DECODE error count=" + std::to_string(error_count) +
                                " accessUnit=" + std::to_string(access_unit_count) +
                                " size=" + std::to_string(unit.data.size()) +
                                " idr=" + std::to_string(unit.idr ? 1 : 0) +
                                " preview=" + HexPreview(unit.data.data(), unit.data.size()) +
                                " decoderDiag=" +
                                (decoder_ ? decoder_->setup_diagnostics() :
                                            std::string("decoder-unavailable")));
                last_logged_decode_error_count_.store(error_count, std::memory_order_relaxed);
            }
            // Corrupt output is signaled independently by the publication
            // gate, including a mixed good/corrupt drain returning positive.
            if(!decoder_ || decoder_->last_submission().codec_error<0 ||
                !decoder_->last_submission().corrupt_frames)
                video_frame_gate_.Fault(false,0);
            if(video_frame_gate_.Required()) {
                if (!decoder_ || !decoder_->uses_hardware_decoder())
                    decoder_reset_requested_.store(true);
                keyframe_needed_.store(true);
            }
        }

        // IDR acceptance (including decoded==0) never clears recovery here.
        // Only the frame gate can confirm its matching clean output PTS.

        {
            std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
            recycle_decode_buffer_locked(std::move(unit.data));
        }
    }

    AppendStreamLog("DECODE worker_stopped");
}

void WebRtcSession::recycle_decode_buffer_locked(std::vector<uint8_t>&& buffer) {
    constexpr size_t kPoolLimit = 8;
    constexpr size_t kMaximumReusableCapacity = 2 * 1024 * 1024;
    if (decoder_buffer_pool_.size() >= kPoolLimit ||
        buffer.capacity() > kMaximumReusableCapacity) {
        return;
    }
    buffer.clear();
    decoder_buffer_pool_.push_back(std::move(buffer));
}

void WebRtcSession::clear_decoder_queue_locked() {
    while (!decoder_queue_.empty()) {
        recycle_decode_buffer_locked(std::move(decoder_queue_.front().data));
        decoder_queue_.pop_front();
    }
}

void WebRtcSession::enqueue_decode_unit(const uint8_t* data, size_t size, uint32_t rtp_timestamp) {
    if (!data || size == 0 || !decoder_running_.load(std::memory_order_acquire))
        return;

    const bool idr = ContainsH264Idr(data, size);
    std::lock_guard<std::mutex> lock(decoder_queue_mutex_);

    if (!video_frame_gate_.Queue(rtp_timestamp,idr)) {
        decoder_queue_drops_++;
        decoder_resync_drops_.fetch_add(1, std::memory_order_relaxed);
        keyframe_needed_.store(true, std::memory_order_release);
        return;
    }

    // Keep the queue bounded to about 67 ms. Crossing the bound means the
    // decoder is no longer at the live edge, so clear the dependent chain and
    // resume at an IDR instead of accumulating visible input latency.
    const auto now = std::chrono::steady_clock::now();
    const auto oldest_age_us = decoder_queue_.empty() ? 0LL :
        std::chrono::duration_cast<std::chrono::microseconds>(
            now - decoder_queue_.front().enqueued_at).count();
    if (opennow::video::DecodeQueueNeedsResync(
            settings_.fps, decoder_queue_.size(), oldest_age_us)) {
        const size_t queued = decoder_queue_.size();
        decoder_queue_drops_ += static_cast<int>(queued);
        decoder_overflow_drops_.fetch_add(queued, std::memory_order_relaxed);
        clear_decoder_queue_locked();
        video_frame_gate_.Fault(false,0);
        if (!decoder_ || !decoder_->uses_hardware_decoder())
            decoder_reset_requested_.store(true);
        keyframe_needed_.store(true);
        if (!idr) {
            decoder_queue_drops_++;
            decoder_overflow_drops_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        (void)video_frame_gate_.Queue(rtp_timestamp,true);
    }

    std::vector<uint8_t> buffer;
    if (!decoder_buffer_pool_.empty()) {
        buffer = std::move(decoder_buffer_pool_.front());
        decoder_buffer_pool_.pop_front();
        decoder_buffer_reuses_.fetch_add(1, std::memory_order_relaxed);
    } else {
        decoder_buffer_allocations_.fetch_add(1, std::memory_order_relaxed);
    }
    buffer.resize(size);
    std::memcpy(buffer.data(), data, size);
    const bool reset_before_idr = idr && video_frame_gate_.Required() &&
        (!decoder_ || !decoder_->uses_hardware_decoder());
    decoder_queue_.push_back({
        std::move(buffer), idr, rtp_timestamp,
        now, reset_before_idr
    });
    AtomicMax(decoder_queue_high_water_, decoder_queue_.size());
    decoder_queue_cv_.notify_one();
}


void WebRtcSession::draw(NVGcontext* vg, int width, int height, AVFrame* frame, uint64_t generation) {
    if (renderer_ && frame) {
        const int previous_width = rendered_video_width_.load(std::memory_order_relaxed);
        const int previous_height = rendered_video_height_.load(std::memory_order_relaxed);
        if (previous_width != frame->width || previous_height != frame->height ||
            renderer_frame_format_ != frame->format) {
            AppendStreamLog("VIDEO output_layout old=" +
                std::to_string(previous_width) + "x" + std::to_string(previous_height) +
                " new=" + std::to_string(frame->width) + "x" + std::to_string(frame->height) +
                " format=" + std::to_string(frame->format) +
                " stride=" + std::to_string(frame->linesize[0]) + "/" +
                std::to_string(frame->linesize[1]) + " renderer_rebuild=1");
            // GFN can change resolution during adaptive streaming. The old
            // renderer owns layouts, copy extents and uniforms for the previous
            // size. Destroy it on the render thread (which waits for GPU use)
            // before interpreting the new frame or releasing its mappings.
            renderer_.reset();
            renderer_ = std::make_unique<DKVideoRenderer>();
            renderer_frame_format_ = frame->format;
        }
        rendered_video_width_.store(frame->width, std::memory_order_relaxed);
        rendered_video_height_.store(frame->height, std::memory_order_relaxed);
        const auto render_started_at = std::chrono::steady_clock::now();
        last_rendered_video_pts_.store(frame->pts);
        renderer_->drawLatest(vg, width, height, frame, VIDEO_FORMAT_H264, generation);
        // GPU resources and their counters belong to the render thread. Publish
        // plain counters so terminal/network diagnostics never dereference them.
        if(const auto* stats=renderer_->video_render_stats()) {
            std::unique_lock<std::mutex> snapshot_lock(render_snapshot_mutex_,std::try_to_lock);
            if(snapshot_lock.owns_lock()) render_stats_snapshot_=*stats;
        }
        if (!video_frame_gate_.Required() && has_decoded_idr_.load() &&
            opennow::video::RtpDelta(static_cast<uint32_t>(frame->pts),
                last_decoded_idr_timestamp_.load()) >= 0) {
            const uint64_t recovery_begin = recovery_present_started_us_.exchange(0);
            if (recovery_begin) {
                const uint64_t elapsed = NowUs() - recovery_begin;
                AtomicMax(recovery_present_max_us_, elapsed);
                AppendStreamLog("VIDEO recovery_present elapsedMs=" + std::to_string(elapsed / 1000));
            }
        }
        const uint64_t render_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - render_started_at).count());
        render_us_total_.fetch_add(render_us, std::memory_order_relaxed);
        RecordLatency(render_latency_buckets_, render_us);
        AtomicMax(render_us_max_, render_us);
        const uint64_t frame_budget_us =
            1000000ULL / static_cast<uint64_t>(std::max(1, settings_.fps));
        if (render_us > frame_budget_us)
            render_late_frames_.fetch_add(1, std::memory_order_relaxed);
        const uint64_t previous_generation = last_presented_generation_.exchange(generation);
        if (generation != 0 && generation != previous_generation)
            presented_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}


void WebRtcSession::maybe_request_startup_keyframe_retry() {
    if (!pc_ || !peer_completed_seen_ || frames_decoded_.load() > 0)
        return;

    const PeerConnectionState state = peer_connection_get_state(pc_);
    if (state != PEER_CONNECTION_COMPLETED)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (last_startup_keyframe_request_.time_since_epoch().count() != 0 &&
        now - last_startup_keyframe_request_ < std::chrono::seconds(3)) {
        return;
    }

    last_startup_keyframe_request_ = now;
    request_keyframe("startup_no_video");
}

void WebRtcSession::maybe_recover_decode_stall() {
    const uint64_t last_decoded_us = last_decoded_frame_at_us_.load(std::memory_order_acquire);
    if (!pc_ || frames_decoded_.load() == 0 || last_decoded_us == 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const uint64_t now_us = NowUs();
    const uint64_t stalled_ms = now_us >= last_decoded_us ? (now_us - last_decoded_us) / 1000 : 0;
    if (stalled_ms < 1500)
        return;

    if (last_decode_stall_request_at_.time_since_epoch().count() != 0 &&
        now - last_decode_stall_request_at_ < std::chrono::seconds(2)) {
        return;
    }

    // A completed connection can still stop producing complete H.264 pictures.
    // Ask for a fresh IDR promptly, but never flood the signaling channel.
    last_decode_stall_request_at_ = now;
    AppendStreamLog("DECODE timeoutMs=" + std::to_string(stalled_ms) +
                    " packets=" + std::to_string(packets_received_.load()) +
                    " frames=" + std::to_string(frames_decoded_.load()));
    {
        std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
        const size_t stalled_queue_size = decoder_queue_.size();
        decoder_queue_drops_ += static_cast<int>(stalled_queue_size);
        decoder_stall_drops_.fetch_add(stalled_queue_size,
                                       std::memory_order_relaxed);
        clear_decoder_queue_locked();
    }
    video_frame_gate_.Fault(false,0);
    if (!decoder_ || !decoder_->uses_hardware_decoder())
        decoder_reset_requested_.store(true);
    request_keyframe("decode_timeout");
}

void WebRtcSession::maybe_recover_rtp_damage() {
    if (!pc_)
        return;
    PeerVideoRtpStats stats {};
    if (peer_connection_get_video_rtp_stats(pc_, &stats) != 0 ||
        stats.access_units_dropped <= last_rtp_access_units_dropped_) {
        return;
    }

    const uint32_t newly_dropped = stats.access_units_dropped - last_rtp_access_units_dropped_;
    last_rtp_access_units_dropped_ = stats.access_units_dropped;
    const auto now = std::chrono::steady_clock::now();
    if (last_rtp_damage_request_at_.time_since_epoch().count() == 0 ||
        now - last_rtp_damage_request_at_ >= std::chrono::seconds(1)) {
        last_rtp_damage_request_at_ = now;
        AppendStreamLog("RTP damage auDroppedNew/total=" + std::to_string(newly_dropped) +
                        "/" + std::to_string(stats.access_units_dropped) +
                        " gaps=" + std::to_string(stats.sequence_gaps) +
                        " reordered=" + std::to_string(stats.reordered_packets) +
                        " late=" + std::to_string(stats.late_packets_dropped) +
                        " forced=" + std::to_string(stats.forced_sequence_skips) +
                        " adaptiveHold/recovered/expired=" +
                        std::to_string(stats.reorder_adaptive_holds) + "/" +
                        std::to_string(stats.reorder_gap_recoveries) + "/" +
                        std::to_string(stats.reorder_gap_expirations) +
                        " graceMs=" +
                        std::to_string(stats.reorder_gap_hold_ms) + "/" +
                        std::to_string(stats.reorder_burst_gap_hold_ms) +
                        " nack/retry/rtx=" +
                        std::to_string(stats.nack_requests) + "/" +
                        std::to_string(stats.nack_retries) + "/" +
                        std::to_string(stats.rtx_packets_recovered));
    }
    // The receive callback may already have handled this exact damaged AU.
    // A delayed poll must not restart recovery after its replacement IDR.
    const uint32_t previous_gate = last_rtp_damage_gate_.exchange(
        stats.access_units_dropped, std::memory_order_acq_rel);
    if (stats.access_units_dropped <= previous_gate ||
        !opennow::video::ShouldResyncForDamagedFrame(
            stats.last_dropped_timestamp,
            last_decoded_idr_timestamp_.load(std::memory_order_acquire),
            has_decoded_idr_.load(std::memory_order_acquire)))
        return;
    // Once a reference picture is missing, reject later P frames and resume at
    // the next IDR. Preserve access units that were completed before the loss:
    // flushing those healthy pictures turned one damaged AU into a visible
    // multi-frame stall and repeatedly reset the software decoder.
    if (video_frame_gate_.Fault(true,stats.last_dropped_timestamp)) {
        std::size_t preserved_queue_size = 0;
        {
            std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
            preserved_queue_size = decoder_queue_.size();
        }
        AppendStreamLog("RTP recovery=hold_until_idr queuePreserved=" +
                        std::to_string(preserved_queue_size));
    }
    // Do not flush healthy queued frames or request an IDR for every damaged
    // access unit. Large IDRs amplify packet bursts and previously created a
    // self-sustaining PLI/loss loop. maybe_recover_decode_stall() remains the
    // authoritative recovery path when output actually stops.
}


void WebRtcSession::begin_video_recovery() {
    uint64_t unset = 0;
    const uint64_t now = NowUs();
    if (recovery_started_us_.compare_exchange_strong(unset, now))
        recovery_pli_retries_.store(0);
    unset = 0;
    recovery_present_started_us_.compare_exchange_strong(unset, now);
}

void WebRtcSession::request_keyframe(const char* reason) {
    const auto now = std::chrono::steady_clock::now();
    const uint64_t now_us = NowUs();
    const uint64_t video_us = last_video_packet_at_us_.load(std::memory_order_acquire);
    const int media_age_ms = video_us && now_us >= video_us
        ? static_cast<int>(std::min<uint64_t>((now_us - video_us) / 1000, 5000)) : 5000;
    const auto minimum_pli_interval = video_frame_gate_.Required()
        ? frames_decoded_.load(std::memory_order_relaxed) == 0
            ? std::chrono::milliseconds(600)
            : std::chrono::milliseconds(opennow::streaming::RecoveryRetryMs(
                  get_network_rtt_ms(), recovery_pli_retries_.load(), media_age_ms,
                  video_us > video_at_last_keyframe_request_us_))
        : std::chrono::milliseconds(1500);
    if (last_keyframe_request_at_.time_since_epoch().count() != 0 &&
        now - last_keyframe_request_at_ < minimum_pli_interval) {
        // Keep one coalesced request pending. poll() will retry it after the
        // cooldown instead of flooding the server with expensive IDR frames.
        if (video_frame_gate_.Required())
            keyframe_needed_.store(true);
        return;
    }

    last_keyframe_request_at_ = now;
    video_at_last_keyframe_request_us_ = video_us;
    if (video_frame_gate_.Required())
        recovery_pli_retries_.fetch_add(1);
    keyframe_request_attempts_++;
    size_t decode_backlog = 0;
    { std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_); decode_backlog = decoder_queue_.size(); }
    const int pli_result = pc_ ? peer_connection_request_video_keyframe(pc_) : -1;
    AppendStreamLog("KEYFRAME request reason=" + std::string(reason ? reason : "switch_stream") +
                    " attempt=" + std::to_string(keyframe_request_attempts_) +
                    " rtcpPli=" + std::to_string(pli_result) +
                    " packets=" + std::to_string(packets_received_.load()) +
                    " frames=" + std::to_string(frames_decoded_.load()) +
                    " decodeQueue=" + std::to_string(decode_backlog) +
                    " mediaAgeMs=" + std::to_string(media_age_ms));

    // Keep one bounded retry armed while damaged reference pictures are being
    // rejected. Decoding a valid IDR clears both flags in decoder_loop().
    if (video_frame_gate_.Required())
        keyframe_needed_.store(true);

    if (pli_result >= 0 || !signaling_client_ || remote_peer_id_ <= 0)
        return;

    // Keep the legacy signaling request as a fallback until the video SSRC is known.
    json_t* req = json_object();
    json_object_set_new(req, "type", json_string("request_keyframe"));
    json_object_set_new(req, "reason", json_string(reason ? reason : "switch_stream"));
    json_object_set_new(req, "backlogFrames", json_integer(decode_backlog));
    json_object_set_new(req, "attempt", json_integer(keyframe_request_attempts_));
    send_peer_payload(req);
    json_decref(req);
}

void WebRtcSession::on_video_packet(const PeerVideoPacket& packet) {
    const auto callback_began_us = NowUs();
    AtomicMax(ingress_assembly_window_us_, ingress_timing_.Unit(callback_began_us));
    const uint8_t* data = packet.data;
    const size_t size = packet.size;
    const bool idr = ContainsH264Idr(data, size);

    // The RTP decoder invokes this callback from the media thread immediately
    // after completing an access unit. Detect a preceding damaged AU here,
    // before another dependent P frame can enter the decode queue. The slower
    // poll path still records diagnostics and sends the coalesced PLI.
    PeerVideoRtpStats immediate_stats {};
    const bool have_immediate_stats = pc_ &&
        peer_connection_get_video_rtp_stats(pc_, &immediate_stats) == 0;
    if (have_immediate_stats) {
        uint32_t previous_damage =
            last_rtp_damage_gate_.load(std::memory_order_relaxed);
        if (immediate_stats.access_units_dropped > previous_damage &&
            last_rtp_damage_gate_.compare_exchange_strong(
                previous_damage,
                immediate_stats.access_units_dropped,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            if (opennow::video::ShouldResyncForDamagedFrame(
                    immediate_stats.last_dropped_timestamp,
                    last_decoded_idr_timestamp_.load(std::memory_order_acquire),
                    has_decoded_idr_.load(std::memory_order_acquire))) {
                video_frame_gate_.Fault(true,immediate_stats.last_dropped_timestamp);
            }
        }
    }

    const uint64_t arrived_us = NowUs();
    if (has_video_au_arrival_) {
        frame_delivery_.Observe(arrived_us - last_video_au_arrival_us_,
            opennow::video::RtpDelta(packet.timestamp, last_video_au_timestamp_),
            90000 / std::max(1, settings_.fps));
        arrival_p95_us_.store(frame_delivery_.P95(), std::memory_order_relaxed);
        arrival_jitter_us_.store(frame_delivery_.jitter_us, std::memory_order_relaxed);
        arrival_over33_.store(frame_delivery_.over33, std::memory_order_relaxed);
        arrival_over50_.store(frame_delivery_.over50, std::memory_order_relaxed);
        arrival_over100_.store(frame_delivery_.over100, std::memory_order_relaxed);
        const auto kind = opennow::video::ClassifyFrameArrival(
            opennow::video::RtpDelta(packet.timestamp, last_video_au_timestamp_),
            arrived_us - last_video_au_arrival_us_,
            90000 / std::max(1, settings_.fps));
        if (kind == opennow::video::FrameArrivalKind::TimestampGap) {
            source_timestamp_gaps_.fetch_add(1, std::memory_order_relaxed);
            if (have_immediate_stats &&
                immediate_stats.access_units_dropped > last_arrival_damage_count_)
                timestamp_gaps_with_loss_.fetch_add(1, std::memory_order_relaxed);
        } else if (kind == opennow::video::FrameArrivalKind::DeliveryGap)
            delivery_arrival_gaps_.fetch_add(1, std::memory_order_relaxed);
        else if (kind == opennow::video::FrameArrivalKind::Burst)
            delivery_bursts_.fetch_add(1, std::memory_order_relaxed);
    }
    if (have_immediate_stats)
        last_arrival_damage_count_ = immediate_stats.access_units_dropped;
    last_video_au_timestamp_ = packet.timestamp;
    last_video_au_arrival_us_ = arrived_us;
    has_video_au_arrival_ = true;
    last_video_packet_at_us_.store(arrived_us, std::memory_order_release);
    video_access_unit_bytes_.fetch_add(size, std::memory_order_relaxed);
    AtomicMax(video_access_unit_max_bytes_, static_cast<uint64_t>(size));
    video_ssrc_ = packet.ssrc;
    for (const auto& report : sender_reports_) {
        if (report.ssrc == video_ssrc_) {
            video_sr_ntp_us_ = report.ntp_us;
            video_sr_rtp_timestamp_ = report.rtp_timestamp;
            have_video_sender_report_ = true;
            break;
        }
    }
    const int packet_count = packets_received_.fetch_add(1) + 1;
    if (!first_video_packet_logged_) {
        first_video_packet_logged_ = true;
        AppendStreamLog("VIDEO first_access_unit size=" + std::to_string(size) +
                        " idr=" + std::to_string(idr ? 1 : 0) +
                        " preview=" + HexPreview(data, size));
        log_stream_summary("first_video_packet");
    } else if (packet_count - last_logged_packet_count_ >= 120) {
        AppendStreamLog("VIDEO access_unit_progress units=" + std::to_string(packet_count) +
                        " size=" + std::to_string(size) +
                        " frames=" + std::to_string(frames_decoded_.load()) +
                        " decodeErrors=" + std::to_string(decode_errors_.load()) +
                        " queueDrops=" + std::to_string(decoder_queue_drops_.load()));
        last_logged_packet_count_ = packet_count;
    }

    enqueue_decode_unit(data, size, packet.timestamp);
    AtomicMax(ingress_callback_window_us_, NowUs() - callback_began_us);
}

void WebRtcSession::on_audio_packet(const PeerAudioPacket& packet) {
    last_audio_packet_at_us_.store(NowUs(), std::memory_order_release);
    const bool new_audio_ssrc = audio_ssrc_ != packet.ssrc;
    audio_ssrc_ = packet.ssrc;
    if (audio_) {
        audio_->submit(packet);
        if (new_audio_ssrc) {
            for (const auto& report : sender_reports_) {
                if (report.ssrc == audio_ssrc_) {
                    audio_->set_sender_report(report.ssrc, report.ntp_us, report.rtp_timestamp);
                    break;
                }
            }
        }
    }
}

void WebRtcSession::on_rtp_sender_report(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp) {
    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    auto found = std::find_if(sender_reports_.begin(), sender_reports_.end(), [ssrc](const SenderReport& item) {
        return item.ssrc == ssrc;
    });
    if (found == sender_reports_.end())
        sender_reports_.push_back({ssrc, ntp_us, rtp_timestamp});
    else
        *found = {ssrc, ntp_us, rtp_timestamp};

    if (ssrc == audio_ssrc_ && audio_)
        audio_->set_sender_report(ssrc, ntp_us, rtp_timestamp);
    if (ssrc == video_ssrc_) {
        video_sr_ntp_us_ = ntp_us;
        video_sr_rtp_timestamp_ = rtp_timestamp;
        have_video_sender_report_ = true;
    }
}

int64_t WebRtcSession::video_target_rtp_timestamp() const {
    // Most GFN attempts provide no video SR. Do not contend for the peer at
    // every vsync just to discover the same absent correlation.
    if (!have_video_sender_report_.load(std::memory_order_acquire)) {
        video_clock_no_video_sr_.fetch_add(1, std::memory_order_relaxed);
        return AV_NOPTS_VALUE;
    }
    // This runs once per displayed frame. Falling back to FIFO presentation
    // for one refresh is preferable to stalling the render thread behind the
    // network worker.
    std::unique_lock<std::recursive_mutex> lock(peer_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        video_clock_lock_skips_.fetch_add(1, std::memory_order_relaxed);
        return AV_NOPTS_VALUE;
    }
    if (!have_video_sender_report_) {
        video_clock_no_video_sr_.fetch_add(1,std::memory_order_relaxed);
        return AV_NOPTS_VALUE;
    }
    const int64_t audio_ntp = audio_ ? audio_->playback_ntp_us() : -1;
    if (audio_ntp < 0) {
        video_clock_no_audio_.fetch_add(1,std::memory_order_relaxed);
        return AV_NOPTS_VALUE;
    }
    video_clock_available_.fetch_add(1,std::memory_order_relaxed);
    return opennow::audio::RtpTimestampAtNtp(
        video_sr_rtp_timestamp_, static_cast<int64_t>(video_sr_ntp_us_), audio_ntp, 90000);
}
