#include "webrtc_session.hpp"
#include "controller_report_queue.hpp"
#include "stream_diagnostics.hpp"
#include "internal.hpp"

#include <jansson.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace opennow::webrtc::internal;


namespace
{

void PutU16Le(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void PutI16Le(std::vector<uint8_t>& out, int16_t value)
{
    PutU16Le(out, static_cast<uint16_t>(value));
}

void PutU32Le(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void PutU64Le(std::vector<uint8_t>& out, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
}

void PutU16Be(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void PutU32Be(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void PutU64Be(std::vector<uint8_t>& out, uint64_t value)
{
    for (int i = 7; i >= 0; --i)
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
}

std::vector<uint8_t> WrapSingleInput(int protocol_version, uint64_t timestamp_us,
                                     const std::vector<uint8_t>& payload)
{
    if (protocol_version <= 2)
        return payload;

    std::vector<uint8_t> wrapped;
    wrapped.reserve(10 + payload.size());
    wrapped.push_back(0x23);
    PutU64Be(wrapped, timestamp_us);
    wrapped.push_back(0x22);
    wrapped.insert(wrapped.end(), payload.begin(), payload.end());
    return wrapped;
}

std::vector<uint8_t> WrapReliableGamepad(int protocol_version, uint64_t timestamp_us,
                                         const std::vector<uint8_t>& payload)
{
    if (protocol_version <= 2)
        return payload;

    std::vector<uint8_t> wrapped;
    wrapped.reserve(12 + payload.size());
    wrapped.push_back(0x23);
    PutU64Be(wrapped, timestamp_us);
    wrapped.push_back(0x21);
    PutU16Be(wrapped, static_cast<uint16_t>(payload.size()));
    wrapped.insert(wrapped.end(), payload.begin(), payload.end());
    return wrapped;
}

std::vector<uint8_t> WrapPartiallyReliableGamepad(int protocol_version, uint64_t timestamp_us,
                                                  uint8_t controller_id, uint16_t sequence,
                                                  const std::vector<uint8_t>& payload)
{
    if (protocol_version <= 2)
        return payload;

    std::vector<uint8_t> wrapped;
    wrapped.reserve(16 + payload.size());
    wrapped.push_back(0x23);
    PutU64Be(wrapped, timestamp_us);
    wrapped.push_back(0x26);
    wrapped.push_back(controller_id);
    PutU16Be(wrapped, sequence);
    wrapped.push_back(0x21);
    PutU16Be(wrapped, static_cast<uint16_t>(payload.size()));
    wrapped.insert(wrapped.end(), payload.begin(), payload.end());
    return wrapped;
}

int16_t AxisToI16(float value)
{
    value = std::max(-1.0f, std::min(1.0f, value));
    return static_cast<int16_t>(value * 32767.0f);
}

std::vector<uint8_t> BuildGamepadPayload(uint64_t timestamp_us, uint8_t controller_id,
                                         uint16_t controller_bitmap, uint16_t buttons,
                                         uint8_t left_trigger, uint8_t right_trigger,
                                         int16_t lx, int16_t ly, int16_t rx, int16_t ry)
{
    std::vector<uint8_t> payload;
    payload.reserve(38);
    PutU32Le(payload, 12);
    PutU16Le(payload, 26);
    PutU16Le(payload, controller_id);
    PutU16Le(payload, controller_bitmap);
    PutU16Le(payload, 20);
    PutU16Le(payload, buttons);
    PutU16Le(payload, static_cast<uint16_t>(left_trigger | (right_trigger << 8)));
    PutI16Le(payload, lx);
    PutI16Le(payload, ly);
    PutI16Le(payload, rx);
    PutI16Le(payload, ry);
    PutU16Le(payload, 0);
    PutU16Le(payload, 85);
    PutU16Le(payload, 0);
    PutU64Le(payload, timestamp_us);
    return payload;
}

std::vector<uint8_t> BuildMouseButtonPayload(uint64_t timestamp_us, bool pressed)
{
    std::vector<uint8_t> payload;
    payload.reserve(18);
    PutU32Le(payload, pressed ? 8u : 9u);
    payload.push_back(1);
    payload.push_back(0);
    PutU32Be(payload, 0);
    PutU64Be(payload, timestamp_us);
    return payload;
}

std::vector<uint8_t> BuildMouseMovePayload(uint64_t timestamp_us, int16_t dx, int16_t dy)
{
    std::vector<uint8_t> payload;
    payload.reserve(22);
    PutU32Le(payload, 7);
    PutU16Be(payload, static_cast<uint16_t>(dx));
    PutU16Be(payload, static_cast<uint16_t>(dy));
    PutU16Be(payload, 0);
    PutU32Be(payload, 0);
    PutU64Be(payload, timestamp_us);
    return payload;
}

std::vector<uint8_t> BuildKeyboardPayload(uint64_t timestamp_us, uint16_t keycode,
                                          uint16_t scancode, uint16_t modifiers,
                                          bool pressed)
{
    std::vector<uint8_t> payload;
    payload.reserve(18);
    PutU32Le(payload, pressed ? 3u : 4u);
    PutU16Be(payload, keycode);
    PutU16Be(payload, modifiers);
    PutU16Be(payload, scancode);
    PutU64Be(payload, timestamp_us);
    return payload;
}

} // namespace

namespace opennow::webrtc::internal
{

bool InputEncodingSelfTest()
{
    constexpr uint64_t timestamp = 0x0102030405060708ULL;
    const auto raw = BuildGamepadPayload(timestamp, 2, 0x0f0f, 0x1234, 0x56, 0x78, 1, -2, 3, -4);
    if (raw.size() != 38 || raw[0] != 12 || raw[6] != 2 ||
        raw[8] != 0x0f || raw[9] != 0x0f ||
        raw[24] != 0 || raw[25] != 0 || raw[26] != 0x55 || raw[27] != 0 ||
        raw[28] != 0 || raw[29] != 0 || raw[30] != 0x08 || raw[37] != 0x01) {
        return false;
    }

    const auto reliable = WrapReliableGamepad(3, timestamp, raw);
    if (reliable.size() != 50 || reliable[0] != 0x23 || reliable[9] != 0x21 ||
        reliable[10] != 0 || reliable[11] != 38 || reliable[12] != 12) {
        return false;
    }

    const auto partial = WrapPartiallyReliableGamepad(3, timestamp, 2, 1, raw);
    if (partial.size() != 54 || partial[0] != 0x23 || partial[9] != 0x26 ||
        partial[10] != 2 || partial[11] != 0 || partial[12] != 1 ||
        partial[13] != 0x21 || partial[14] != 0 || partial[15] != 38 || partial[16] != 12) {
        return false;
    }

    const auto mouse = BuildMouseButtonPayload(timestamp, true);
    const auto wrapped_mouse = WrapSingleInput(3, timestamp, mouse);
    const auto move = BuildMouseMovePayload(timestamp, 10, -5);
    const auto wrapped_move = WrapReliableGamepad(3, timestamp, move);
    const auto key = BuildKeyboardPayload(timestamp, 0x41, 0x1e, 0, true);
    return mouse.size() == 18 && mouse[0] == 8 && mouse[4] == 1 && mouse[17] == 0x08 &&
           wrapped_mouse.size() == 28 && wrapped_mouse[0] == 0x23 &&
           wrapped_mouse[9] == 0x22 && wrapped_mouse[10] == 8 &&
           move.size() == 22 && move[0] == 7 && move[4] == 0 && move[5] == 10 &&
           move[6] == 0xff && move[7] == 0xfb && wrapped_move.size() == 34 &&
           wrapped_move[9] == 0x21 && wrapped_move[10] == 0 && wrapped_move[11] == 22 &&
           key.size() == 18 && key[0] == 3 && key[4] == 0 && key[5] == 0x41 &&
           key[8] == 0 && key[9] == 0x1e;
}

} // namespace opennow::webrtc::internal

bool WebRtcSession::send_gamepad_input(
    uint8_t controller_id,
    uint16_t controller_bitmap,
    uint16_t buttons,
    uint8_t left_trigger,
    uint8_t right_trigger,
    float lx,
    float ly,
    float rx,
    float ry) {
    if (controller_id >= gamepad_sequences_.size())
        return false;

    const uint64_t attempt_us = NowUs();
    if (last_input_attempt_us_ && attempt_us >= last_input_attempt_us_)
        AtomicMax(input_interval_us_max_, attempt_us - last_input_attempt_us_);
    last_input_attempt_us_ = attempt_us;
    gamepad_input_attempt_count_++;

    // Controller sampling runs on the UI/render thread. Sending through SCTP
    // used to wait for the peer mutex here, so an incoming UDP burst could
    // freeze rendering and input together for 20-30 ms. Keep only the newest
    // report for each controller and let the transport thread send it while it
    // already owns the peer connection. A changed state is retried by the
    // controller delivery policy if the input channel is not ready yet.
    if (stop_requested_.load(std::memory_order_acquire) ||
        !input_delivery_ready_.load(std::memory_order_acquire)) {
        gamepad_input_blocked_count_++;
        if (gamepad_input_blocked_count_ <= 8 || gamepad_input_blocked_count_ % 50 == 0) {
            AppendInputLog("PAD blocked attempt=" + std::to_string(gamepad_input_attempt_count_) +
                           " deliveryReady=" +
                               std::to_string(input_delivery_ready_.load() ? 1 : 0) +
                           " buttons=0x" + HexPreview(reinterpret_cast<const uint8_t*>(&buttons), sizeof(buttons), 2));
        }
        return false;
    }

    const uint64_t timestamp_us = NowUs();
    const std::vector<uint8_t> payload = BuildGamepadPayload(
        timestamp_us, controller_id, controller_bitmap, buttons, left_trigger, right_trigger,
        AxisToI16(lx), AxisToI16(-ly), AxisToI16(rx), AxisToI16(-ry));

    const int protocol_version = input_protocol_version_.load(std::memory_order_acquire);
    // The bundled SCTP backend uses the negotiated reliable channel. Keeping
    // this decision local also avoids reading mutable peer state on the UI
    // thread while it builds the coalesced report.
    const bool use_partial = false;
    std::vector<uint8_t> wire_payload = use_partial
        ? WrapPartiallyReliableGamepad(protocol_version, timestamp_us, controller_id,
                                       gamepad_sequences_[controller_id]++, payload)
        : WrapReliableGamepad(protocol_version, timestamp_us, payload);
    const uint16_t sid = use_partial ? 2 : 0;

    {
        std::lock_guard<std::mutex> queue_lock(outbound_input_mutex_);
        auto& queued = pending_gamepad_reports_[controller_id];
        PendingGamepadReport report;
        report.wire_payload = std::move(wire_payload);
        report.sid = sid;
        report.controller_id = controller_id;
        report.queued_at_us = attempt_us;
        report.attempt = static_cast<uint64_t>(gamepad_input_attempt_count_);
        report.buttons = opennow::input::ControllerEdgeState(buttons, left_trigger, right_trigger);
        // Axis-only changes may be replaced, but button edges must stay in
        // order even when several UI reports arrive in one transport batch.
        constexpr size_t kMaximumButtonEdges = 8;
        const auto queued_result = opennow::input::QueueControllerReport(
            queued, std::move(report), kMaximumButtonEdges);
        if (queued_result == opennow::input::ReportQueueResult::CoalescedAxes)
            gamepad_reports_coalesced_.fetch_add(1, std::memory_order_relaxed);
        else if (queued_result == opennow::input::ReportQueueResult::DroppedOldestEdge)
            gamepad_button_edges_dropped_.fetch_add(1, std::memory_order_relaxed);
        input_wake_generation_.fetch_add(1, std::memory_order_release);
        interactive_input_pending_.store(true, std::memory_order_release);
    }
    network_wake_cv_.notify_one();
    return true;
}

int WebRtcSession::flush_pending_gamepad_inputs_locked(size_t maximum_reports) {
    if (!pc_ || !input_ready_ ||
        peer_connection_get_state(pc_) != PEER_CONNECTION_COMPLETED)
        return 0;
    if (NowUs() < input_backpressure_until_us_)
        return 0;

    int flushed = 0;
    const auto batch_started_us = NowUs();
    const size_t first_index = next_gamepad_report_;
    for (size_t checked = 0;
         checked < pending_gamepad_reports_.size() &&
             static_cast<size_t>(flushed) < maximum_reports;
         ++checked) {
        const size_t index =
            (first_index + checked) % pending_gamepad_reports_.size();
        PendingGamepadReport report;
        {
            std::lock_guard<std::mutex> queue_lock(outbound_input_mutex_);
            if (pending_gamepad_reports_[index].empty())
                continue;
            report = std::move(pending_gamepad_reports_[index].front());
            pending_gamepad_reports_[index].pop_front();
        }

        const uint64_t send_started_us = NowUs();
        if (report.queued_at_us > 0 && send_started_us >= report.queued_at_us) {
            const uint64_t wait_us = send_started_us - report.queued_at_us;
            AtomicMax(input_queue_wait_us_max_, wait_us);
            input_queue_window_.Observe(wait_us);
            RecordLatency(input_queue_wait_buckets_, wait_us);
        }
        const int sent = send_datachannel_binary(
            report.sid, "gamepad", report.wire_payload.data(),
            report.wire_payload.size());
        const uint64_t send_us = NowUs() - send_started_us;
        AtomicMax(input_send_us_max_, send_us);
        input_send_window_.Observe(send_us);
        RecordLatency(input_send_buckets_, send_us);
        if (sent == SCTP_SEND_WINDOW_FULL) {
            input_backpressure_count_.fetch_add(1);
            input_backpressure_until_us_ = NowUs() + 15000;
        } else if (sent < 0) {
            gamepad_send_failure_count_++;
        } else {
            gamepad_tx_count_++;
        }

        if (report.attempt <= 12 || gamepad_tx_count_ <= 8 ||
            report.attempt % 100 == 0 || sent < 0) {
            AppendInputLog("PAD tx attempt=" + std::to_string(report.attempt) +
                           " report=" + std::to_string(gamepad_tx_count_) +
                           " sid=" + std::to_string(report.sid) +
                           " controller=" + std::to_string(report.controller_id) +
                           " queuedUs=" +
                               std::to_string(send_started_us - report.queued_at_us) +
                           " wireBytes=" +
                               std::to_string(report.wire_payload.size()) +
                           " sent=" + std::to_string(sent));
        }
        if (sent < 0) {
            std::lock_guard<std::mutex> queue_lock(outbound_input_mutex_);
            pending_gamepad_reports_[index].push_front(std::move(report));
        }
        next_gamepad_report_ = (index + 1) % pending_gamepad_reports_.size();
        flushed++;
        if (sent == SCTP_SEND_WINDOW_FULL ||
            opennow::input::EndInputBatch(static_cast<unsigned>(flushed), NowUs() - batch_started_us))
            break;
    }

    bool pending = false;
    {
        std::lock_guard<std::mutex> queue_lock(outbound_input_mutex_);
        for (const auto& report : pending_gamepad_reports_)
            pending = pending || !report.empty();
        interactive_input_pending_.store(pending, std::memory_order_release);
    }
    return flushed;
}

bool WebRtcSession::has_pending_gamepad_inputs() const {
    std::lock_guard<std::mutex> queue_lock(outbound_input_mutex_);
    return std::any_of(pending_gamepad_reports_.begin(), pending_gamepad_reports_.end(),
        [](const auto& reports) { return !reports.empty(); });
}

void WebRtcSession::clear_pending_gamepad_inputs() {
    std::lock_guard<std::mutex> queue_lock(outbound_input_mutex_);
    for (auto& report : pending_gamepad_reports_)
        report.clear();
    interactive_input_pending_.store(false, std::memory_order_release);
}

void WebRtcSession::send_mouse_left_button(bool pressed) {
    interactive_input_pending_.store(true, std::memory_order_release);
    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    interactive_input_pending_.store(has_pending_gamepad_inputs(), std::memory_order_release);
    if (!pc_ || !input_ready_ || peer_connection_get_state(pc_) != PEER_CONNECTION_COMPLETED) {
        AppendInputLog("MOUSE blocked pressed=" + std::to_string(pressed ? 1 : 0) +
                       " pc=" + std::to_string(pc_ ? 1 : 0) +
                       " inputReady=" + std::to_string(input_ready_ ? 1 : 0));
        return;
    }

    const uint64_t timestamp_us = NowUs();
    const std::vector<uint8_t> payload = BuildMouseButtonPayload(timestamp_us, pressed);
    std::vector<uint8_t> wire_payload = WrapSingleInput(input_protocol_version_, timestamp_us, payload);
    const int sent = send_datachannel_binary(0, pressed ? "mouse_left_down" : "mouse_left_up",
                                             wire_payload.data(), wire_payload.size());
    if (sent >= 0)
        mouse_tx_count_++;
    AppendInputLog("MOUSE tx pressed=" + std::to_string(pressed ? 1 : 0) +
                   " sent=" + std::to_string(sent) +
                   " bytes=" + std::to_string(wire_payload.size()) +
                   " hex=" + HexPreview(wire_payload.data(), wire_payload.size(), 32));
}

void WebRtcSession::send_mouse_move(int16_t dx, int16_t dy) {
    constexpr int16_t kSafeMouseDelta = 4096;
    const int16_t safe_dx = std::clamp(dx, static_cast<int16_t>(-kSafeMouseDelta), kSafeMouseDelta);
    const int16_t safe_dy = std::clamp(dy, static_cast<int16_t>(-kSafeMouseDelta), kSafeMouseDelta);
    if (safe_dx != dx || safe_dy != dy) {
        AppendInputLog("MOUSE_MOVE clamped requested=" + std::to_string(dx) + "/" +
                       std::to_string(dy) + " safe=" + std::to_string(safe_dx) + "/" +
                       std::to_string(safe_dy));
    }
    dx = safe_dx;
    dy = safe_dy;
    if (dx == 0 && dy == 0)
        return;

    interactive_input_pending_.store(true, std::memory_order_release);
    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    interactive_input_pending_.store(has_pending_gamepad_inputs(), std::memory_order_release);
    if (!pc_ || !input_ready_ || peer_connection_get_state(pc_) != PEER_CONNECTION_COMPLETED) {
        AppendInputLog("MOUSE_MOVE blocked dx=" + std::to_string(dx) +
                       " dy=" + std::to_string(dy));
        return;
    }

    const uint64_t timestamp_us = NowUs();
    const auto payload = BuildMouseMovePayload(timestamp_us, dx, dy);
    const auto wire_payload = input_protocol_version_ <= 2
        ? payload
        : WrapReliableGamepad(input_protocol_version_, timestamp_us, payload);
    const int sent = send_datachannel_binary(0, "mouse_move", wire_payload.data(), wire_payload.size());
    AppendInputLog("MOUSE_MOVE tx dx=" + std::to_string(dx) +
                   " dy=" + std::to_string(dy) + " sent=" + std::to_string(sent));
}

bool WebRtcSession::send_keyboard_key(uint16_t keycode, uint16_t scancode,
                                      uint16_t modifiers, bool pressed) {
    interactive_input_pending_.store(true, std::memory_order_release);
    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    interactive_input_pending_.store(has_pending_gamepad_inputs(), std::memory_order_release);
    if (!pc_ || !input_ready_ || peer_connection_get_state(pc_) != PEER_CONNECTION_COMPLETED) {
        if (!opennow::SensitiveInputLoggingSuppressed())
            AppendInputLog("KEY blocked vk=" + std::to_string(keycode) +
                           " pressed=" + std::to_string(pressed ? 1 : 0));
        return false;
    }

    const uint64_t timestamp_us = NowUs();
    const auto payload = BuildKeyboardPayload(timestamp_us, keycode, scancode, modifiers, pressed);
    const auto wire_payload = WrapSingleInput(input_protocol_version_, timestamp_us, payload);
    const int sent = send_datachannel_binary(0, pressed ? "key_down" : "key_up",
                                             wire_payload.data(), wire_payload.size());
    if (!opennow::SensitiveInputLoggingSuppressed())
        AppendInputLog("KEY tx vk=" + std::to_string(keycode) +
                       " scan=" + std::to_string(scancode) +
                       " mods=" + std::to_string(modifiers) +
                       " pressed=" + std::to_string(pressed ? 1 : 0) +
                       " sent=" + std::to_string(sent));
    return sent >= 0;
}


void WebRtcSession::on_datachannel_message(const char* msg, size_t len, uint16_t sid) {
    std::string preview;
    if (msg && len > 0)
        preview.assign(msg, msg + std::min<size_t>(len, 180));

    const char* label_raw = pc_ ? peer_connection_lookup_sid_label(pc_, sid) : nullptr;
    const std::string label = label_raw ? label_raw : "";
    AppendStreamLog("DATA rx sid=" + std::to_string(sid) +
                    " label=" + (label.empty() ? std::string("(unknown)") : label) +
                    " bytes=" + std::to_string(len) +
                    " hex=" + HexPreview(reinterpret_cast<const uint8_t*>(msg), len, 16) +
                    " preview=" + PreviewText(preview, 120));
    AppendInputLog("RX sid=" + std::to_string(sid) +
                   " label=" + (label.empty() ? std::string("unknown") : label) +
                   " bytes=" + std::to_string(len) +
                   " hex=" + HexPreview(reinterpret_cast<const uint8_t*>(msg), len, 24));

    if (msg && len >= 2 && (label.empty() || label == "input_channel_v1" || sid == 0)) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(msg);
        const uint16_t first_word = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
        int version = 2;
        bool handshake = false;

        if (first_word == 526) {
            version = len >= 4 ? static_cast<int>(bytes[2] | (bytes[3] << 8)) : 2;
            handshake = true;
        } else if (bytes[0] == 0x0e) {
            version = first_word;
            handshake = true;
        }

        if (handshake) {
            input_ready_ = true;
            input_delivery_ready_.store(true, std::memory_order_release);
            input_protocol_version_ = std::max(2, version);
            AppendStreamLog("DATA input_handshake_complete version=" +
                            std::to_string(input_protocol_version_) +
                            " firstWord=" + std::to_string(first_word));
            AppendInputLog("HANDSHAKE complete protocol=" + std::to_string(input_protocol_version_) +
                           " firstWord=" + std::to_string(first_word));
            // The network worker sends the first heartbeat after draining input.
        }
    }
}

void WebRtcSession::on_datachannel_open() {
    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    datachannel_opened_ = true;
    consecutive_input_send_failures_ = 0;
    AppendStreamLog("DATA sctp_open attempts=" + std::to_string(datachannel_open_attempts_) +
                    " requested=" + std::to_string(datachannel_open_requested_ ? 1 : 0));
    AppendInputLog("SCTP associationOpen=1 attempts=" + std::to_string(datachannel_open_attempts_));
}

void WebRtcSession::on_datachannel_close() {
    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    AppendStreamLog("DATA sctp_close");
    AppendInputLog("SCTP associationOpen=0 reason=callback_close");
    reset_input_channel_for_retry("sctp_close");
}

int WebRtcSession::next_ack_id() {
    ack_counter_ += 1;
    return ack_counter_;
}

void WebRtcSession::send_peer_info() {
    if (cloud_client_profile_==opennow::gfn::cloud_session::ClientProfile::BrowserWebRtc ||
        !signaling_client_ || signaling_registration_.Confirmed() ||
        signaling_registration_.Attempts()>=opennow::signaling::RegistrationPolicy::MaximumAttempts)
        return;

    json_t* root = json_object();
    const auto registration_request_id=next_ack_id();
    json_object_set_new(root, "ackid", json_integer(registration_request_id));

    json_t* info = json_object();
    json_object_set_new(info, "browser", json_string("Chrome"));
    json_object_set_new(info, "browserVersion", json_string("131"));
    json_object_set_new(info, "connected", json_true());
    json_object_set_new(info, "id", json_integer(peer_id_));
    json_object_set_new(info, "name", json_string(peer_name_.c_str()));
    json_object_set_new(info, "peerRole", json_integer(0));
    const std::string resolution = std::to_string(settings_.width) + "x" + std::to_string(settings_.height);
    json_object_set_new(info, "resolution", json_string(resolution.c_str()));
    json_object_set_new(info, "version", json_integer(2));
    json_object_set_new(root, "peer_info", info);

    char* dump = json_dumps(root, 0);
    if (dump) {
        AppendStreamLog("TX " + std::string(dump));
        const bool written=signaling_client_->send_message(std::string(dump));
        signaling_registration_.Sent(registration_request_id,written,NowUs());
        AppendStreamLog(signaling_diagnostics_.Registration(registration_request_id,written,std::strlen(dump)));
        free(dump);
    } else {
        signaling_registration_.Sent(registration_request_id,false,NowUs());
        AppendStreamLog(signaling_diagnostics_.Registration(registration_request_id,false,0));
    }
    json_decref(root);
}

void WebRtcSession::send_peer_payload(json_t* payload) {
    if (!signaling_client_ || !payload)
        return;

    char* payload_dump = json_dumps(payload, 0);
    if (!payload_dump)
        return;

    json_t* root = json_object();
    json_t* peer_msg = json_object();
    json_object_set_new(peer_msg, "from", json_integer(peer_id_));
    json_object_set_new(peer_msg, "to", json_integer(remote_peer_id_));
    json_object_set_new(peer_msg, "msg", json_string(payload_dump));
    json_object_set_new(root, "peer_msg", peer_msg);
    json_object_set_new(root, "ackid", json_integer(next_ack_id()));

    char* dump = json_dumps(root, 0);
    if (dump) {
        AppendStreamLog("TX " + std::string(dump));
        AppendTraceLog("TX peer_payload " + PreviewText(std::string(payload_dump), 500));
        signaling_client_->send_message(std::string(dump));
        free(dump);
    }

    free(payload_dump);
    json_decref(root);
}

void WebRtcSession::send_heartbeat() {
    if (!signaling_client_)
        return;

    json_t* heartbeat = json_object();
    json_object_set_new(heartbeat, "hb", json_integer(1));
    char* dump = json_dumps(heartbeat, 0);
    if (dump) {
        heartbeat_tx_count_++;
        AppendStreamLog("TX " + std::string(dump));
        signaling_client_->send_message(std::string(dump));
        free(dump);
    }
    json_decref(heartbeat);
    last_heartbeat_sent_ = std::chrono::steady_clock::now();
}

void WebRtcSession::maybe_send_keepalive() {
    if (!signaling_ready_ || !signaling_client_)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (last_heartbeat_sent_.time_since_epoch().count() == 0 ||
        now - last_heartbeat_sent_ >= std::chrono::seconds(5)) {
        send_heartbeat();
    }

    if (!offer_received_.load() && signaling_registration_.ShouldSend(
            signaling_client_->is_connected(),false,NowUs())) {
        send_peer_info();
    }
}


void WebRtcSession::maybe_open_datachannel() {
    if (!pc_ || datachannel_open_requested_)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (last_datachannel_open_attempt_.time_since_epoch().count() != 0 &&
        now - last_datachannel_open_attempt_ < std::chrono::milliseconds(750)) {
        return;
    }

    if (datachannel_open_attempts_ >= 60)
        return;

    last_datachannel_open_attempt_ = now;
    datachannel_open_attempts_++;

    char protocol[] = "";

    if (!reliable_input_channel_requested_) {
        char reliable_label[] = "input_channel_v1";
        const int ret = peer_connection_create_datachannel_sid(
            pc_,
            DATA_CHANNEL_RELIABLE,
            0,
            0,
            reliable_label,
            protocol,
            0);

        AppendStreamLog("DATA create_channel label=input_channel_v1 sid=0 type=reliable ret=" +
                        std::to_string(ret) +
                        " attempt=" + std::to_string(datachannel_open_attempts_));
        AppendInputLog("DCEP create sid=0 label=input_channel_v1 attempt=" +
                       std::to_string(datachannel_open_attempts_) +
                       " result=" + std::to_string(ret) +
                       " sctpOpen=" + std::to_string(datachannel_opened_ ? 1 : 0));

        if (ret >= 0)
            reliable_input_channel_requested_ = true;
    }

    // The Switch fallback SCTP backend intentionally uses one reliable channel.
    // Partial reliability requires Forward-TSN negotiation and is optional for GFN input.
    datachannel_open_requested_ = reliable_input_channel_requested_;
    if (datachannel_open_requested_ && input_activation_due_.time_since_epoch().count() == 0) {
        // Prefer the server handshake; use v2 only as a compatibility fallback.
        input_activation_due_ = now + std::chrono::milliseconds(1500);
        AppendStreamLog("DATA fast_channel disabled reason=internal_sctp_no_forward_tsn");
        AppendInputLog("DCEP reliableChannelRequested=1 fastChannel=disabled activationDelayMs=1500");
    }
}

void WebRtcSession::reset_input_channel_for_retry(const char* reason) {
    datachannel_opened_ = false;
    datachannel_open_requested_ = false;
    reliable_input_channel_requested_ = false;
    partial_input_channel_requested_ = false;
    input_ready_ = false;
    input_delivery_ready_.store(false, std::memory_order_release);
    clear_pending_gamepad_inputs();
    startup_control_sent_ = false;
    input_activation_due_ = {};
    last_datachannel_open_attempt_ = {};
    last_input_heartbeat_sent_ = {};
    consecutive_input_send_failures_ = 0;
    input_protocol_version_.store(2, std::memory_order_release);
    AppendInputLog(std::string("RECOVERY input_channel_reset reason=") +
                   (reason ? reason : "unknown") +
                   " attempts=" + std::to_string(datachannel_open_attempts_));
}

void WebRtcSession::note_input_send_result(int sent, const char* label) {
    // A full reliable window means ACKs are late. Keep the negotiated channel
    // and protocol intact; the pending controller report is retried on ACK.
    if (sent == SCTP_SEND_WINDOW_FULL)
        return;
    if (sent >= 0) {
        consecutive_input_send_failures_ = 0;
        return;
    }

    consecutive_input_send_failures_++;
    if (consecutive_input_send_failures_ < 3)
        return;

    // Do not keep acknowledging controller states locally while SCTP rejects
    // them. Pause briefly, then reactivate the negotiated reliable channel.
    input_ready_ = false;
    input_delivery_ready_.store(false, std::memory_order_release);
    input_activation_due_ = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(250);
    AppendInputLog(std::string("RECOVERY input_send_failures label=") +
                   (label ? label : "unknown") +
                   " consecutive=" + std::to_string(consecutive_input_send_failures_) +
                   " action=reactivate");
    consecutive_input_send_failures_ = 0;
}

void WebRtcSession::maybe_activate_input() {
    if (input_ready_ || !datachannel_open_requested_ || !datachannel_opened_ || !pc_)
        return;

    if (input_activation_due_.time_since_epoch().count() == 0 ||
        std::chrono::steady_clock::now() < input_activation_due_)
        return;

    input_ready_ = true;
    input_delivery_ready_.store(true, std::memory_order_release);
    const int protocol = input_protocol_version_.load(std::memory_order_acquire);
    AppendStreamLog("DATA input_ready protocol=" + std::to_string(protocol) +
                    " controller=xbox sidReliable=0 sidFast=2");
    AppendInputLog("HANDSHAKE reactivated protocol=" + std::to_string(protocol) +
                   " inputReady=1 controller=xbox sid=0");
}

void WebRtcSession::send_datachannel_text(const std::string& label, const std::string& payload) {
    if (!pc_ || payload.empty())
        return;

    const int sent = peer_connection_datachannel_send(pc_, const_cast<char*>(payload.c_str()), payload.size());
    AppendStreamLog("DATA tx label=" + label +
                    " opened=" + std::to_string(datachannel_opened_ ? 1 : 0) +
                    " sent=" + std::to_string(sent) +
                    " bytes=" + std::to_string(payload.size()) +
                    " payload=" + PreviewText(payload, 220));
}

int WebRtcSession::send_datachannel_binary(uint16_t sid, const std::string& label, const uint8_t* payload, size_t size) {
    if (!pc_ || !payload || size == 0)
        return -1;

    const uint64_t sctp_started_us = NowUs();
    const int sent = peer_connection_datachannel_send_binary_sid(
        pc_,
        const_cast<char*>(reinterpret_cast<const char*>(payload)),
        size,
        sid);
    const auto send_elapsed_us = NowUs() - sctp_started_us;
    AtomicMax(input_sctp_us_max_, send_elapsed_us);
    if (label == "gamepad") AtomicMax(gamepad_send_window_max_us_, send_elapsed_us);
    if (label == "input_heartbeat") AtomicMax(heartbeat_send_window_max_us_, send_elapsed_us);
    if (send_elapsed_us >= 8000 && (label == "gamepad" || label == "input_heartbeat"))
        AppendInputLog("INPUT_SEND label=" + label + " elapsedUs=" + std::to_string(send_elapsed_us) +
            " result=" + std::to_string(sent));
    note_input_send_result(sent, label.c_str());
    // Controller reports are sent every frame; logging each one would create
    // input latency and make the useful SCTP diagnostics unreadable.
    const bool high_frequency_input = label == "gamepad" || label == "input_heartbeat";
    if (opennow::CanLogInputPayload(label) && (!high_frequency_input || gamepad_tx_count_ < 5 || gamepad_tx_count_ % 120 == 0)) {
        AppendStreamLog("DATA tx-binary label=" + label +
                        " sid=" + std::to_string(sid) +
                        " sent=" + std::to_string(sent) +
                        " bytes=" + std::to_string(size) +
                        " hex=" + HexPreview(payload, size, 16));
    }
    return sent;
}

void WebRtcSession::maybe_send_input_heartbeat() {
    if (!pc_ || !input_ready_ || !reliable_input_channel_requested_)
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto now_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
    const auto previous_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(last_input_heartbeat_sent_.time_since_epoch()).count());
    if (!opennow::input::HeartbeatDue(now_us, previous_us,
            interactive_input_pending_.load(std::memory_order_acquire))) return;

    last_input_heartbeat_sent_ = now;
    input_heartbeat_tx_count_++;

    uint8_t heartbeat[4] = {2, 0, 0, 0}; // INPUT_HEARTBEAT, little-endian.
    const int sent = send_datachannel_binary(
        0, "input_heartbeat", heartbeat, sizeof(heartbeat));

    if (input_heartbeat_tx_count_ <= 5 || input_heartbeat_tx_count_ % 20 == 0) {
        AppendStreamLog("DATA input_heartbeat sent=" + std::to_string(sent) +
                        " count=" + std::to_string(input_heartbeat_tx_count_) +
                        " protocol=" + std::to_string(input_protocol_version_));
        AppendInputLog("HEARTBEAT tx count=" + std::to_string(input_heartbeat_tx_count_) +
                       " sent=" + std::to_string(sent) +
                       " protocol=" + std::to_string(input_protocol_version_));
    }
}

void WebRtcSession::maybe_send_startup_control_messages() {
    if (!pc_ || startup_control_sent_ || !datachannel_open_requested_)
        return;

    startup_control_sent_ = true;
    AppendStreamLog("DATA startup_channels requested reliable=" +
                    std::to_string(reliable_input_channel_requested_ ? 1 : 0) +
                    " partial=" + std::to_string(partial_input_channel_requested_ ? 1 : 0) +
                    " thresholdMs=" + std::to_string(partial_reliable_threshold_ms_));
}
