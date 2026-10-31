#include "ad_report_policy.hpp"

#include <cassert>

int main()
{
    using namespace opennow;

    const auto started = ad_report::Started("first");
    assert(started.action == SessionAdAction::Start);
    assert(started.watched_time_ms == -1);
    assert(started.paused_time_ms == 0);

    const auto finished = ad_report::Finished("second", 29977, 30000);
    assert(finished.action == SessionAdAction::Finish);
    assert(finished.watched_time_ms == 29977);
    assert(finished.paused_time_ms == 0);

    const auto overrun = ad_report::Finished("second", 30042, 30000);
    assert(overrun.watched_time_ms == 30000);

    assert(ad_report::IsRecoverableTransitionError(
        "ReportSessionAd failed: statusCode=5, INVALID_REQUEST_STATUS"));
    assert(!ad_report::IsRecoverableTransitionError(
        "ReportSessionAd failed: HTTP 401"));

    const auto cancelled = ad_report::Cancelled("third", -1, "playback_error");
    assert(cancelled.action == SessionAdAction::Cancel);
    assert(cancelled.watched_time_ms == 0);
    assert(cancelled.paused_time_ms == 0);
    assert(cancelled.cancel_reason == "playback_error");
    const auto not_started = ad_report::PlaybackFailed("empty-vast", 0);
    assert(not_started.action == SessionAdAction::Cancel);
    assert(not_started.cancel_reason == "error");
    assert(not_started.watched_time_ms == 0);
    assert(not_started.paused_time_ms == 0);
    const auto failed_during_playback = ad_report::PlaybackFailed("decoder-error", 1200);
    assert(failed_during_playback.action == SessionAdAction::Cancel);
    assert(failed_during_playback.watched_time_ms == 1200);
    assert(failed_during_playback.cancel_reason == "error");
    assert(ad_report::PlaybackFailed("missing-media", -1).watched_time_ms == 0);
    ad_report::FailureReports recovery;
    int sent = 0;
    const auto accept = [&](const SessionAdReport& report) {
        ++sent;
        assert(report.ad_id == "empty-vast");
        assert(report.action == SessionAdAction::Cancel);
        assert(report.watched_time_ms == 0 && report.cancel_reason == "error");
        return true;
    };
    // A source failure before Start still sends exactly one error update.
    assert(recovery.Report("empty-vast", 0, accept));
    assert(recovery.Accepted("empty-vast"));
    assert(recovery.HasAccepted());
    assert(recovery.Report("empty-vast", 0, accept));
    assert(sent == 1);
    const auto defer = [&](const SessionAdReport&) { ++sent; return false; };
    for (int attempt = 0; attempt < 3; ++attempt) {
        assert(!recovery.Report("pending", 0, defer));
        assert(!recovery.Accepted("pending"));
    }
    bool limited = false;
    try { recovery.Report("pending", 0, defer); }
    catch (const std::runtime_error&) { limited = true; }
    assert(limited && sent == 4);
    // The next ad has an independent report budget; no finish is fabricated.
    assert(recovery.Attempts("replacement") == 0);
    assert(recovery.Report("replacement", 500, [](const SessionAdReport& report) {
        return report.action == SessionAdAction::Cancel && report.watched_time_ms == 500;
    }));
    return 0;
}
