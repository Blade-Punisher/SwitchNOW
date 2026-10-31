#pragma once

#include <chrono>

namespace opennow
{

enum class PeerTerminalKind
{
    None,
    Failed,
    Disconnected,
    Closed,
};

enum class StreamEndReason
{
    None,
    FreeSessionEnded,
    NetworkLost,
    ServerDisconnected,
    ConnectionFailed,
    StreamEnded,
    VideoTimedOut,
    StartupTimedOut,
};

struct StreamEndSignals
{
    bool startup_timed_out = false;
    bool free_tier = false;
    std::chrono::seconds session_elapsed {0};
    std::chrono::seconds startup_elapsed {0};
    bool internet_connected = true;
    bool peer_completed = false;
    PeerTerminalKind peer_terminal = PeerTerminalKind::None;
    bool signaling_connected = true;
    bool video_started = false;
    bool video_decoded = false;
    std::chrono::milliseconds video_idle {0};
    std::chrono::milliseconds network_unavailable {0};
};

inline StreamEndReason DetectStreamEnd(const StreamEndSignals& signals)
{
    constexpr auto kFreeLimit = std::chrono::hours(1);
    constexpr auto kFreeTerminalGrace = std::chrono::minutes(59);
    if (signals.free_tier &&
        (signals.session_elapsed >= kFreeLimit ||
         (signals.session_elapsed >= kFreeTerminalGrace &&
          signals.peer_terminal != PeerTerminalKind::None)))
    {
        return StreamEndReason::FreeSessionEnded;
    }

    if (!signals.internet_connected &&
        signals.network_unavailable >= std::chrono::seconds(5) &&
        (!signals.video_started || signals.video_idle >= std::chrono::seconds(3)) &&
        (signals.peer_completed || signals.video_started ||
         signals.peer_terminal != PeerTerminalKind::None))
    {
        return StreamEndReason::NetworkLost;
    }

    if (signals.startup_timed_out && !signals.video_decoded &&
        signals.peer_terminal != PeerTerminalKind::Closed)
        return StreamEndReason::StartupTimedOut;

    switch (signals.peer_terminal)
    {
        case PeerTerminalKind::Failed:
            return signals.peer_completed
                ? StreamEndReason::ServerDisconnected
                : StreamEndReason::ConnectionFailed;
        case PeerTerminalKind::Disconnected:
            return StreamEndReason::ServerDisconnected;
        case PeerTerminalKind::Closed:
            return StreamEndReason::StreamEnded;
        case PeerTerminalKind::None:
            break;
    }

    if (signals.peer_completed && !signals.signaling_connected &&
        signals.video_started && signals.video_idle >= std::chrono::seconds(8))
    {
        return StreamEndReason::ServerDisconnected;
    }

    if (signals.video_decoded && signals.video_started &&
        signals.video_idle >= std::chrono::seconds(15))
        return StreamEndReason::VideoTimedOut;

    // ICE/DTLS completion alone cannot disarm the first-picture deadline.
    if (signals.peer_completed && !signals.video_decoded &&
        signals.startup_elapsed >= std::chrono::seconds(25))
        return StreamEndReason::StartupTimedOut;

    return StreamEndReason::None;
}

} // namespace opennow
