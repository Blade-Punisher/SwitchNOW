#include "ad_break_policy.hpp"

#include <cassert>
#include <iostream>

int main()
{
    using namespace opennow;

    AdBreakInfo value;
    assert(ad_break::Classify(value) == ad_break::PresentationState::None);

    value.required = true;
    assert(ad_break::Classify(value) == ad_break::PresentationState::AwaitingMetadata);

    SessionAdInfo ad;
    ad.duration_seconds = 30;
    ad.media_files.push_back({"https://example.test/ad.m3u8", "hlsadaptive"});
    ad.media_files.push_back({"https://example.test/ad.webm", "webm"});
    ad.media_files.push_back({"https://example.test/ad.mp4", "mp4deinterlaced720p"});
    value.ads.push_back(ad);

    assert(ad_break::Classify(value) == ad_break::PresentationState::MediaReady);
    assert(ad_break::TotalDurationSeconds(value) == 30);
    const auto* preferred = ad_break::SelectPreferredMedia(value);
    assert(preferred != nullptr);
    assert(preferred->encoding_profile == "mp4deinterlaced720p");

    const auto* per_ad_preferred = ad_break::SelectPreferredMedia(ad);
    assert(per_ad_preferred != nullptr);
    assert(per_ad_preferred->encoding_profile == "mp4deinterlaced720p");

    AdBreakInfo no_ad_session;
    no_ad_session.ads.push_back(ad);
    assert(ad_break::Classify(no_ad_session) == ad_break::PresentationState::None);

    SessionAdInfo hls_only;
    hls_only.media_files.push_back({"https://example.test/ad.m3u8", "hlsadaptive"});
    assert(ad_break::SelectPreferredMedia(hls_only) != nullptr);
    assert(ad_break::SelectPreferredMedia(hls_only)->encoding_profile == "hlsadaptive");

    SessionAdInfo no_media;
    assert(ad_break::SelectPreferredMedia(no_media) == nullptr);
    assert(ad_break::IsWaitingAdsTimeout(79, ""));
    assert(ad_break::IsWaitingAdsTimeout(0, "SESSION_WAITING_ADS_TIME_EXPIRED 4A8C3011"));
    assert(!ad_break::IsWaitingAdsTimeout(18, "UNRELATED"));

    // Replay the R24 queue progression after Cancel(error) was accepted:
    // NVIDIA retains adsRequired=true, ads=[], while positions keep changing.
    SessionInfo waiting;
    waiting.status = 1;
    waiting.ad_break.required = true;
    for (const int position : {40, 36, 34, 18, 15, 16}) {
        waiting.queue_position = position;
        const auto screen = ad_break::QueuePresentation(waiting);
        assert(screen.show_position);
        assert(std::string(screen.title) == "Position in queue:{0}");
    }
    waiting.queue_position = 0;
    auto screen = ad_break::QueuePresentation(waiting);
    assert(!screen.show_position);
    assert(std::string(screen.title) == "Preparing your cloud rig");
    waiting.ad_break.ads.push_back(ad);
    waiting.queue_position = 20;
    assert(ad_break::QueuePresentation(waiting).show_position);
    waiting.queue_position = 0;
    waiting.status = 0;
    assert(std::string(ad_break::QueuePresentation(waiting).title) == "Waiting in queue...");
    waiting.status = 1;
    waiting.app_patching = true;
    assert(std::string(ad_break::QueuePresentation(waiting).title) == "Updating the game");
    waiting.queue_position = 42;
    assert(!ad_break::QueuePresentation(waiting).show_position);
    waiting.app_patching = false;
    for (const int status : {2, 3, 4}) {
        waiting.status = status;
        assert(!ad_break::QueuePresentation(waiting).show_position);
    }
    waiting.status = 0;
    waiting.queue_position = -1;
    assert(!ad_break::QueuePresentation(waiting).show_position);
    assert(std::string(ad_break::QueuePresentation(waiting).title) == "Waiting in queue...");
    assert(ad_break::QueuePollDelayMs(waiting) == 3000);
    waiting.status = 1;
    waiting.seat_setup_step = 1;
    assert(std::string(ad_break::QueuePresentation(waiting).title) == "Waiting in queue...");
    waiting.seat_setup_step = -1;
    waiting.queue_position = 10;
    assert(ad_break::QueuePollDelayMs(waiting) == 2000);
    waiting.queue_position = 126;
    assert(ad_break::QueuePollDelayMs(waiting) == 5000);
    waiting.queue_position = 131; // queue priority changes can legitimately increase it
    assert(ad_break::QueuePresentation(waiting).show_position);

    std::cout << "ad break policy tests passed\n";
    return 0;
}
