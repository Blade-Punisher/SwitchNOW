#pragma once

#include "models.hpp"

#include <algorithm>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace opennow::ad_report
{

inline SessionAdReport Started(const std::string& ad_id)
{
    SessionAdReport report;
    report.ad_id = ad_id;
    report.action = SessionAdAction::Start;
    report.paused_time_ms = 0;
    return report;
}

inline SessionAdReport Finished(
    const std::string& ad_id, int watched_time_ms,
    int expected_duration_ms = 0)
{
    SessionAdReport report;
    report.ad_id = ad_id;
    report.action = SessionAdAction::Finish;
    // Match the browser implementation: report the observed playback clock,
    // not an inflated declared duration. Clamp small container overruns to the
    // advertised length because CloudMatch rejects impossible watch times.
    report.watched_time_ms = std::max(0, watched_time_ms);
    if (expected_duration_ms > 0)
        report.watched_time_ms = std::min(
            report.watched_time_ms, expected_duration_ms);
    report.paused_time_ms = 0;
    return report;
}

inline bool IsRecoverableTransitionError(const std::string& error)
{
    return error.find("INVALID_REQUEST_STATUS") != std::string::npos ||
           error.find("statusCode=5") != std::string::npos;
}

inline SessionAdReport Cancelled(
    const std::string& ad_id, int watched_time_ms, std::string reason)
{
    SessionAdReport report;
    report.ad_id = ad_id;
    report.action = SessionAdAction::Cancel;
    report.watched_time_ms = std::max(0, watched_time_ms);
    report.paused_time_ms = 0;
    report.cancel_reason = std::move(reason);
    return report;
}

// Loading errors also happen before the first frame/Start report. The browser
// reports Cancel("error") in that case so the server can resolve the opportunity.
inline SessionAdReport PlaybackFailed(const std::string& ad_id, int watched_time_ms)
{
    return Cancelled(ad_id, watched_time_ms, "error");
}

class FailureReports
{
public:
    bool HasAccepted() const { return !accepted_.empty(); }
    bool Accepted(const std::string& id) const { return accepted_.count(id) != 0; }
    int Attempts(const std::string& id) const {
        const auto found = attempts_.find(id);
        return found == attempts_.end() ? 0 : found->second;
    }
    template<class Submit>
    bool Report(const std::string& id, int watched_ms, Submit submit) {
        if (Accepted(id)) return true;
        int& attempts = attempts_[id];
        if (attempts >= 3) throw std::runtime_error(
            "NVIDIA did not accept the advertisement error report. "
            "The cloud session remains pending.");
        ++attempts;
        const bool accepted = submit(PlaybackFailed(id, watched_ms));
        if (accepted) accepted_.insert(id);
        return accepted;
    }
private:
    std::unordered_map<std::string, int> attempts_;
    std::unordered_set<std::string> accepted_;
};

} // namespace opennow::ad_report
