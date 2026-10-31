#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace opennow::performance {
enum Field : unsigned {
    TimeMs, Epoch, WindowMs, TargetFps, IncomingX100, DecodedX100, DisplayedX100, BitrateKbps,
    RttMs, RttMedianMs, RttP95Ms, RttJitterMs, RttSamples, RttSourceCode, RttAgeMs,
    Gaps, Late, Damaged, Nack, DecodeP95Us, DecodeMaxUs, QueueWaitP95Us, RenderP95Us,
    RenderMaxUs, DecodeDepth, UiTickMaxUs, UiTicksOver25Ms,
    InputQueueMaxUs, InputSendMaxUs, InputIntervalMaxUs, TransportBatchMaxUs, InputBackpressure,
    TransportPauses, TransportPauseMs, SourcePauses, AssemblyPauses, UncertainPauses, PlaybackPauses, PlaybackPauseMs,
    AudioPlc, AudioUnderruns, AudioErrors, AudioQueuedMs, InputReady, PeerReady,
    VideoStarted, Assessment, RttSnapshotAgeMs, RttSnapshotFresh,
    InputQueueWindowUs, InputSendWindowUs, TransportBatchWindowUs, TransportServiceWindowUs, WarmingUp,
    AudioArrivalJitterUs, AudioStartTargetMs, AudioLimitedBlocks, AudioGainX100, AttemptId,
    Terminal, Recovering, IncomingUnits, DecodedUnits, PresentedUnits, RtpPackets, Reordered, RtxRecovered,
    RepeatedFrames, PresentationDrops, ArrivalP95Us, ArrivalJitterUs, ArrivalOver33, ArrivalOver50, ArrivalOver100,
    ReadyFrames, ReadyAgeTotalUs, ReadyDepth3, LatencyTrims, LatencyBackoffs, UiLogicMaxUs, UiFrameMaxUs,
    LiveMaintenanceWindowUs, LiveMaintenanceDeferrals, InputIdleWaitSkips, Flags, Count
};
using Row = std::array<std::int64_t, Count>;
enum Trigger : unsigned { Network = 1, Incoming = 2, Decoder = 4, Display = 8,
                         Audio = 16, Ui = 32, Rtt = 64, Controls = 128, ClientStall = 256, Delivery = 512 };
unsigned FlagsFor(const Row& row);
unsigned SeverityFor(const Row& row);
std::string CsvHeader(bool incidents);
class Recorder {
public:
    static constexpr unsigned kHistory = 256, kIncidents = 16, kContext = 7;
    void ObserveTick(std::uint64_t now_us);
    void Record(Row row);
    std::string TimelineCsv() const;
    std::string IncidentsCsv() const;
    std::string Summary() const;
    std::uint64_t samples() const { return samples_; }
    std::uint64_t incidents() const { return incidents_; }
    std::uint64_t Total(Field field) const { return totals_[field]; }
    std::uint64_t observed_video_ms() const { return observed_video_ms_; }
    std::uint64_t low_display_ms() const { return low_display_ms_; }
private:
    struct Incident {
        std::array<Row, kContext> rows {};
        unsigned size = 0, after = 0, flags = 0;
        unsigned severity = 0;
        std::uint64_t id = 0;
    };
    std::array<Row, kHistory> history_ {};
    std::array<Incident, kIncidents> events_ {};
    unsigned head_ = 0, size_ = 0;
    std::array<unsigned, 3> event_heads_ {};
    std::array<std::int64_t, 3> last_incident_ms_ {{-10000, -10000, -10000}};
    std::uint64_t samples_ = 0, incidents_ = 0, slow_windows_ = 0, last_tick_ = 0;
    std::uint64_t ui_max_ = 0, ui_slow_ = 0, ui_slow_total_ = 0;
    std::int64_t epoch_ = -1;
    unsigned epoch_samples_ = 0;
    std::int64_t first_video_ms_ = -1;
    std::uint64_t observed_video_ms_ = 0, incoming_area_ = 0, decoded_area_ = 0, displayed_area_ = 0;
    std::uint64_t low_display_ms_ = 0, low_incoming_ms_ = 0;
    std::uint64_t audio_underruns_total_ = 0, playback_pause_ms_total_ = 0, transport_pause_ms_total_ = 0;
    std::array<std::uint64_t, Count> totals_ {};
    std::uint64_t excluded_terminal_ms_ = 0, transport_epochs_ = 0;
};
} // namespace opennow::performance
