#pragma once

#include "models.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace opennow::ad_break
{

enum class PresentationState
{
    None,
    AwaitingMetadata,
    MediaReady,
};

inline PresentationState Classify(const AdBreakInfo& value)
{
    if (!value.required)
        return PresentationState::None;
    return value.ads.empty() ? PresentationState::AwaitingMetadata
                             : PresentationState::MediaReady;
}

struct WaitingPresentation
{
    const char* title;
    const char* detail;
    bool show_position;
};

// Queue and rig preparation remain visible even while NVIDIA keeps its
// advertisement-required flag set after accepting a loading-error report.
inline WaitingPresentation QueuePresentation(const SessionInfo& session)
{
    if (session.app_patching)
        return {"Updating the game", "Setting up rig... Please wait.", false};
    if (session.status == 2 || session.status == 3)
        return {"Connecting to your cloud rig...", "Setting up rig... Please wait.", false};
    if (session.status > 3 && session.status != 6)
        return {"Waiting...", "Setting up rig... Please wait.", false};
    if (session.queue_position > 0)
        return {"Position in queue:{0}", "Waiting for an available cloud rig", true};
    if (session.status == 0 || session.seat_setup_step == 1)
        return {"Waiting in queue...", "Your cloud rig will start automatically when it is ready.", false};
    return {session.app_patching ? "Updating the game" : "Preparing your cloud rig",
            "Setting up rig... Please wait.", false};
}

inline int QueuePollDelayMs(const SessionInfo& session) {
    if (session.app_patching || session.queue_position > 10 || session.status == 6)
        return 5000;
    // Refresh near the head of the queue and during rig preparation. There is
    // no client countdown: only a new successful server reply changes position.
    return session.queue_position >= 0 ? 2000 : 3000;
}

inline int TotalDurationSeconds(const AdBreakInfo& value)
{
    int total = 0;
    for (const auto& ad : value.ads)
        total += std::max(0, ad.duration_seconds);
    return total;
}

inline std::string Lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline int MediaPreferenceScore(const AdMediaFileInfo& media)
{
    const std::string value = Lowercase(media.encoding_profile + " " + media.url);
    if (value.find("mp4") != std::string::npos)
        return 300;
    if (value.find("hls") != std::string::npos || value.find("m3u8") != std::string::npos)
        return 200;
    if (value.find("webm") != std::string::npos)
        return 100;
    return media.url.empty() ? 0 : 10;
}

inline const AdMediaFileInfo* SelectPreferredMedia(const AdBreakInfo& value)
{
    const AdMediaFileInfo* best = nullptr;
    int best_score = -1;
    for (const auto& ad : value.ads)
    {
        for (const auto& media : ad.media_files)
        {
            const int score = MediaPreferenceScore(media);
            if (score > best_score)
            {
                best = &media;
                best_score = score;
            }
        }
    }
    return best;
}

inline const AdMediaFileInfo* SelectPreferredMedia(const SessionAdInfo& value)
{
    const AdMediaFileInfo* best = nullptr;
    int best_score = -1;
    for (const auto& media : value.media_files)
    {
        const int score = MediaPreferenceScore(media);
        if (score > best_score)
        {
            best = &media;
            best_score = score;
        }
    }
    return best;
}

inline bool IsWaitingAdsTimeout(int status_code, std::string_view description)
{
    return status_code == 79 ||
           description.find("SESSION_WAITING_ADS_TIME_EXPIRED") != std::string_view::npos;
}

} // namespace opennow::ad_break
