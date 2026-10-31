#include "stream_end_policy.hpp"

#include <cassert>

int main()
{
    using namespace opennow;
    StreamEndSignals signals;

    signals.free_tier = true;
    signals.session_elapsed = std::chrono::hours(1);
    assert(DetectStreamEnd(signals) == StreamEndReason::FreeSessionEnded);

    signals = {};
    signals.peer_completed = true;
    signals.video_started = true;
    signals.video_decoded = true;
    signals.internet_connected = false;
    assert(DetectStreamEnd(signals) == StreamEndReason::None);
    signals.network_unavailable = std::chrono::seconds(30);
    assert(DetectStreamEnd(signals) == StreamEndReason::None); // Live video wins.
    signals.video_idle = std::chrono::seconds(6);
    signals.network_unavailable = std::chrono::milliseconds(4999);
    assert(DetectStreamEnd(signals) == StreamEndReason::None);
    signals.network_unavailable = std::chrono::seconds(5);
    assert(DetectStreamEnd(signals) == StreamEndReason::NetworkLost);

    signals = {};
    signals.peer_completed = true;
    signals.peer_terminal = PeerTerminalKind::Disconnected;
    assert(DetectStreamEnd(signals) == StreamEndReason::ServerDisconnected);

    signals = {};
    signals.peer_terminal = PeerTerminalKind::Failed;
    assert(DetectStreamEnd(signals) == StreamEndReason::ConnectionFailed);
    signals.startup_timed_out = true;
    assert(DetectStreamEnd(signals) == StreamEndReason::StartupTimedOut);
    signals.peer_terminal = PeerTerminalKind::Closed;
    assert(DetectStreamEnd(signals) == StreamEndReason::StreamEnded);

    signals = {};
    signals.peer_completed = true;
    signals.video_started = true;
    signals.video_decoded = true;
    signals.signaling_connected = false;
    signals.video_idle = std::chrono::seconds(8);
    assert(DetectStreamEnd(signals) == StreamEndReason::ServerDisconnected);

    signals.signaling_connected = true;
    signals.video_idle = std::chrono::seconds(15);
    assert(DetectStreamEnd(signals) == StreamEndReason::VideoTimedOut);

    signals.video_idle = std::chrono::seconds(2);
    assert(DetectStreamEnd(signals) == StreamEndReason::None);
    signals = {};
    signals.peer_completed = true;
    signals.session_elapsed = std::chrono::minutes(20); // Resuming an older cloud session.
    signals.startup_elapsed = std::chrono::seconds(24);
    assert(DetectStreamEnd(signals) == StreamEndReason::None);
    signals.startup_elapsed = std::chrono::seconds(25);
    assert(DetectStreamEnd(signals) == StreamEndReason::StartupTimedOut);
    signals.video_started = true; // RTP without a decoded picture is insufficient.
    signals.video_idle = std::chrono::seconds(20);
    assert(DetectStreamEnd(signals) == StreamEndReason::StartupTimedOut);
    signals.video_idle = std::chrono::seconds(0);
    signals.video_decoded = true;
    assert(DetectStreamEnd(signals) == StreamEndReason::None);
    signals.video_decoded = false;
    signals.peer_completed = false;
    assert(DetectStreamEnd(signals) == StreamEndReason::None);
    return 0;
}
