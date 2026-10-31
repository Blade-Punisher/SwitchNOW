#include "performance_recorder.hpp"
#include <cassert>
#include <algorithm>

int main() {
    using namespace opennow::performance;
    const auto header = CsvHeader(false);
    assert(std::count(header.begin(), header.end(), ',') == Count - 1);
    assert(header.ends_with("trigger_flags\n"));
    Row row {};
    row[TargetFps] = 60;
    row[IncomingX100] = row[DecodedX100] = row[DisplayedX100] = 6000;
    row[InputReady] = row[PeerReady] = row[VideoStarted] = 1;
    assert(FlagsFor(row) == 0);
    row[TargetFps] = 30;
    row[UiTicksOver25Ms] = 1; row[UiTickMaxUs] = 34000;
    assert(!(FlagsFor(row) & Ui)); // Normal 30 FPS pacing is not a UI fault.
    row[TargetFps] = 60;
    assert(FlagsFor(row) & Ui);
    row[UiTicksOver25Ms] = row[UiTickMaxUs] = 0;
    row[AudioUnderruns] = 1;
    assert(FlagsFor(row) & Audio);
    row[AudioUnderruns] = 0;
    row[RttMs] = 80; row[RttMedianMs] = 40;
    assert(!(FlagsFor(row) & Rtt)); // Cached/unconfirmed ping is not a new spike.
    row[RttSnapshotFresh] = 1;
    assert(FlagsFor(row) & Rtt);
    row[RttSourceCode] = 1; row[RttAgeMs] = 5000;
    assert(!(FlagsFor(row) & Rtt)); // Fresh copy of an old STUN probe is still old.
    row[RttAgeMs] = 0;
    row[RttMs] = -1; row[RttMedianMs] = -1;
    assert(!(FlagsFor(row) & Rtt));
    row[Late] = 3;
    assert(!(FlagsFor(row) & Network));
    row[InputSendMaxUs] = 75630; row[TransportBatchMaxUs] = 100663;
    assert(!(FlagsFor(row) & ClientStall));
    row[InputSendWindowUs] = 75630;
    assert(FlagsFor(row) & ClientStall);
    row[InputSendWindowUs] = 0;
    row[TransportServiceWindowUs] = 100663;
    assert(FlagsFor(row) & ClientStall);
    row[TransportServiceWindowUs] = 0;
    row[IncomingX100] = row[DecodedX100] = row[DisplayedX100] = 3000;
    assert(!(FlagsFor(row) & Network) && (FlagsFor(row) & Delivery));
    row[IncomingX100] = row[DecodedX100] = row[DisplayedX100] = 6000;
    row[Late] = 0;
    Recorder recorder;
    for (int i = 0; i < 8; ++i) {
        row[TimeMs] = i * 750;
        recorder.ObserveTick(100000 + i * 16667);
        if (i == 4) row[DisplayedX100] = 1000;
        else row[DisplayedX100] = 6000;
        recorder.Record(row);
    }
    assert(recorder.incidents() == 1);
    const auto events = recorder.IncidentsCsv();
    assert(std::count(events.begin(), events.end(), '\n') == 8); // header + 3 before/event/3 after
    assert(events.starts_with(CsvHeader(true)));
    assert(recorder.TimelineCsv().starts_with(CsvHeader(false)));
    // Long session: keep bounded history and the last 16 incident clusters.
    for (int i = 8; i < 12000; ++i) {
        row[TimeMs] = i * 750;
        row[DisplayedX100] = i % 12 == 0 ? 1000 : 6000;
        recorder.Record(row);
    }
    assert(recorder.samples() == 12000);
    const auto timeline = recorder.TimelineCsv();
    assert(std::count(timeline.begin(), timeline.end(), '\n') == Recorder::kHistory + 1);
    const auto bounded = recorder.IncidentsCsv();
    assert(std::count(bounded.begin(), bounded.end(), '\n') <= Recorder::kIncidents * Recorder::kContext + 1);
    assert(recorder.Summary().find("performance_samples=12000") != std::string::npos);
    Recorder startup;
    row[DisplayedX100] = 0;
    row[Epoch] = 2;
    row[TimeMs] = 750; startup.Record(row);
    row[TimeMs] = 1500; startup.Record(row);
    assert(startup.incidents() == 0); // Warm-up does not generate false incidents.
    row[TimeMs] = 2250; startup.Record(row);
    assert(startup.incidents() == 0);
    row[TimeMs] = 3000; startup.Record(row);
    assert(startup.incidents() == 1);
    row[Epoch] = 3; row[TimeMs] = 9000; startup.Record(row);
    assert(startup.incidents() == 1); // Reconnect restarts warm-up.
    // Hundreds of minor observations must not evict the critical context.
    for (int i = 0; i < 200; ++i) {
        row[TimeMs] = 20000 + i * 7000;
        row[DisplayedX100] = 6000; row[Damaged] = 1;
        startup.Record(row);
    }
    assert(startup.Summary().find("retained_critical=1") != std::string::npos);
    Recorder whole;
    Row sample {};
    sample[TargetFps] = 60; sample[VideoStarted] = sample[PeerReady] = 1; sample[WindowMs] = 1000;
    for (int i = 0; i <= 6; ++i) {
        sample[TimeMs] = i * 1000;
        sample[IncomingX100] = sample[DecodedX100] = i == 3 ? 3000 : 6000;
        sample[DisplayedX100] = i == 3 ? 3000 : i == 4 ? 4500 : 6000;
        sample[AudioUnderruns] = i == 3 ? 2 : 0;
        sample[PlaybackPauseMs] = i == 3 ? 200 : 0;
        whole.Record(sample);
    }
    sample[TimeMs] = 7000; sample[Terminal] = 1;
    sample[IncomingX100] = sample[DecodedX100] = sample[DisplayedX100] = 0;
    sample[PlaybackPauseMs] = sample[AudioUnderruns] = 0;
    whole.Record(sample);
    assert(whole.Summary().find("excluded_terminal_ms=1000") != std::string::npos);
    assert(FlagsFor(sample) == 0);
    const auto summary = whole.Summary();
    assert(summary.find("observed_video_ms=5000") != std::string::npos);
    assert(summary.find("mean_incoming_fps_x100=5400") != std::string::npos);
    assert(summary.find("mean_displayed_fps_x100=5100") != std::string::npos);
    assert(summary.find("low_incoming_ms=1000 low_display_ms=2000") != std::string::npos);
    assert(summary.find("audio_underruns_total=2 playback_pause_ms_total=200") != std::string::npos);
    Recorder multiple;
    Row transport {};
    transport[IncomingUnits] = 100; transport[RtpPackets] = 800; transport[RepeatedFrames] = 2;
    multiple.Record(transport); transport[Epoch] = 1; transport[IncomingUnits] = 30;
    multiple.Record(transport);
    assert(multiple.Total(IncomingUnits) == 130 && multiple.Total(RepeatedFrames) == 4);
    assert(multiple.Summary().find("transport_epochs=2") != std::string::npos);
    Recorder ready_totals;
    Row age{}; age[TargetFps]=60; age[VideoStarted]=1; age[WindowMs]=750;
    age[TimeMs]=1000; age[ReadyFrames]=2; age[ReadyAgeTotalUs]=70000;
    ready_totals.Record(age);
    age[TimeMs]=1750; age[ReadyFrames]=3; age[ReadyAgeTotalUs]=90000; age[LatencyTrims]=1;
    ready_totals.Record(age);
    age[TimeMs]=2500; age[ReadyFrames]=1; age[ReadyAgeTotalUs]=20000; age[LatencyTrims]=0; age[LatencyBackoffs]=1;
    ready_totals.Record(age);
    assert(ready_totals.Total(ReadyFrames)==6);
    assert(ready_totals.Total(LatencyTrims)==1);
    assert(ready_totals.Total(LatencyBackoffs)==1);
    assert(ready_totals.Summary().find("ready_age_mean_us=30000")!=std::string::npos);
    assert(ready_totals.Summary().find("latency_trims_total=1")!=std::string::npos);
    assert(ready_totals.Summary().find("latency_backoffs_total=1")!=std::string::npos);
    assert(CsvHeader(false).find("latency_trims_new")!=std::string::npos);

    Recorder input_priority;
    Row priority{};
    priority[LiveMaintenanceWindowUs]=18000;
    priority[LiveMaintenanceDeferrals]=2;
    priority[InputIdleWaitSkips]=1;
    input_priority.Record(priority);
    priority[LiveMaintenanceWindowUs]=0;
    priority[LiveMaintenanceDeferrals]=0;
    priority[InputIdleWaitSkips]=3;
    input_priority.Record(priority);
    assert(input_priority.Total(LiveMaintenanceDeferrals)==2);
    assert(input_priority.Total(InputIdleWaitSkips)==4);
    assert(input_priority.Summary().find("live_maintenance_deferrals_total=2")!=std::string::npos);
    assert(input_priority.Summary().find("input_idle_wait_skips_total=4")!=std::string::npos);
    const auto priority_csv=input_priority.TimelineCsv();
    assert(priority_csv.find("live_maintenance_max_window_us")!=std::string::npos);
    assert(priority_csv.find("live_maintenance_deferrals_new,input_idle_wait_skips_new,trigger_flags")!=std::string::npos);
    assert(priority_csv.find(",18000,2,1,0\n")!=std::string::npos);

}
