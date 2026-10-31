#include "home_artwork_toast.hpp"
#include "ui_helpers.hpp"
#include "session_error_policy.hpp"
#include "server_assignment_policy.hpp"

#include "ad_playback.hpp"
#include "ad_break_policy.hpp"
#include "ad_report_policy.hpp"
#include "app_state.hpp"
#include "cover_image_cache.hpp"
#include "play_history.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"
#include <borealis.hpp>
#include <switch.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include "StreamView.hpp"

namespace opennow
{
namespace
{

struct LaunchSessionState
{
    std::atomic<bool> running {true};
    std::mutex mutex;
    std::string session_id;
};

class LaunchAnimationView final : public brls::View
{
  public:
    LaunchAnimationView()
    {
        setWidth(620);
        setHeight(168);
    }

    void SetState(int source_stage, float progress)
    {
        visual_stage_ = source_stage <= 1 ? 0 : (source_stage == 2 ? 1 : 2);
        progress_ = std::clamp(progress, 0.04f, 0.98f);
        invalidate();
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override
    {
        (void)style;
        (void)ctx;
        constexpr float kPi = 3.14159265358979323846f;
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(seconds * 3.2));
        static constexpr const char* kLabels[] = {"QUEUE", "SETUP", "READY"};
        const float rail_y = y + 42.0f;
        const float first_x = x + 76.0f;
        const float gap = (width - 152.0f) * 0.5f;

        nvgBeginPath(vg);
        nvgRect(vg, first_x, rail_y - 2.0f, gap * 2.0f, 4.0f);
        nvgFillColor(vg, nvgRGB(38, 42, 48));
        nvgFill(vg);

        float completed_width = visual_stage_ * gap;
        if (visual_stage_ < 2)
            completed_width += gap * std::clamp((progress_ - visual_stage_ * 0.32f) / 0.64f, 0.0f, 0.90f);
        else
            completed_width = gap * 2.0f;
        nvgBeginPath(vg);
        nvgRect(vg, first_x, rail_y - 2.0f, completed_width, 4.0f);
        nvgFillColor(vg, nvgRGB(77, 218, 130));
        nvgFill(vg);

        nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (int i = 0; i < 3; ++i)
        {
            const float cx = first_x + gap * i;
            const bool complete = i < visual_stage_;
            const bool active = i == visual_stage_;
            if (active)
            {
                nvgBeginPath(vg);
                nvgCircle(vg, cx, rail_y, 29.0f + pulse * 3.0f);
                nvgFillColor(vg, nvgRGBA(77, 218, 130, 25 + static_cast<int>(pulse * 22.0f)));
                nvgFill(vg);
            }
            nvgBeginPath(vg);
            nvgCircle(vg, cx, rail_y, 22.0f);
            nvgFillColor(vg, complete || active ? nvgRGB(77, 218, 130) : nvgRGB(24, 28, 33));
            nvgFill(vg);
            nvgStrokeWidth(vg, 2.0f);
            nvgStrokeColor(vg, complete || active ? nvgRGB(108, 235, 153) : nvgRGB(43, 48, 55));
            nvgStroke(vg);

            nvgFontSize(vg, 17.0f);
            nvgFillColor(vg, complete || active ? nvgRGB(8, 35, 22) : nvgRGB(95, 101, 111));
            const std::string number = std::to_string(i + 1);
            nvgText(vg, cx, rail_y, number.c_str(), nullptr);
            nvgFontSize(vg, 12.0f);
            nvgFillColor(vg, complete || active ? nvgRGB(228, 235, 232) : nvgRGB(91, 96, 105));
            const std::string stage_label = Tr(kLabels[i]);
            nvgText(vg, cx, rail_y + 42.0f, stage_label.c_str(), nullptr);
        }

        const float spinner_x = x + width * 0.5f;
        const float spinner_y = y + 137.0f;
        const float angle = static_cast<float>(
            std::fmod(seconds * 3.0, static_cast<double>(kPi) * 2.0));
        nvgBeginPath(vg);
        nvgCircle(vg, spinner_x, spinner_y, 18.0f);
        nvgStrokeWidth(vg, 5.0f);
        nvgStrokeColor(vg, nvgRGB(35, 43, 48));
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgArc(vg, spinner_x, spinner_y, 18.0f, angle,
               angle + kPi * 1.42f, NVG_CW);
        nvgStrokeWidth(vg, 5.0f);
        nvgLineCap(vg, NVG_ROUND);
        nvgStrokeColor(vg, nvgRGB(77, 218, 130));
        nvgStroke(vg);
        const float head_angle = angle + kPi * 1.42f;
        nvgBeginPath(vg);
        nvgCircle(vg, spinner_x + std::cos(head_angle) * 18.0f,
                  spinner_y + std::sin(head_angle) * 18.0f, 3.2f);
        nvgFillColor(vg, nvgRGB(123, 242, 166));
        nvgFill(vg);
    }

  private:
    float progress_ = 0.08f;
    int visual_stage_ = 0;
};

void SetLaunchProgress(
    LaunchAnimationView* animation,
    brls::Label* stage_label,
    brls::Label* detail_label,
    int stage,
    const std::string& title,
    const std::string& detail,
    float progress)
{
    if (animation)
        animation->SetState(stage, progress);
    if (stage_label)
    {
        const bool queue_position = title.rfind("Position in queue:", 0) == 0;
        if (queue_position)
        {
            const std::string value = title.substr(std::string("Position in queue:").size());
            stage_label->setText(TrFormat("Position in queue:{0}", {value}));
        }
        else
        {
            stage_label->setText(Tr(title));
        }
        stage_label->setFontSize(queue_position ? 36 : 27);
        stage_label->setTextColor(queue_position
            ? nvgRGB(98, 232, 148)
            : nvgRGB(238, 242, 245));
    }
    if (detail_label)
    {
        detail_label->setText(Tr(detail));
        detail_label->setFontSize(
            title.rfind("Position in queue:", 0) == 0 ? 18 : 16);
    }
}

std::string SessionStatusText(const SessionInfo& info)
{
    if (info.app_patching)
        return Tr("NVIDIA is updating this game...\nThe session will start automatically when patching finishes.");

    if (info.status == 0 && info.queue_position > 0)
        return TrFormat("In queue... Position: {0}",
                        {std::to_string(info.queue_position)});
    if (info.status == 0 || (info.seat_setup_step == 1 && info.queue_position <= 0))
        return Tr("Waiting in queue...");

    if (info.status == 1)
    {
        if (info.queue_position > 0)
            return TrFormat("Setting up rig... Position: {0}",
                            {std::to_string(info.queue_position)});

        return Tr("Setting up rig... Please wait.");
    }

    if (info.status == 6)
        return Tr("Waiting for NVIDIA session ads or confirmation...");

    return TrFormat("Waiting... Status: {0}", {std::to_string(info.status)});
}

} // namespace

void ShowDialog(const std::string& title, const std::string& body)
{
    auto* dialog = new brls::Dialog(Tr(title) + "\n\n" + TrMessage(body));
    dialog->addButton(Tr("Close"), [] {});
    dialog->setCancelable(true);
    dialog->open();
}

void ShowModalDialog(const std::string& title, const std::string& body)
{
    auto* dialog = new brls::Dialog(Tr(title) + "\n\n" + TrMessage(body));
    dialog->addButton(Tr("Close"), [] {});
    dialog->setCancelable(false);
    dialog->open();
}

void ShowError(const std::string& title, const std::string& body)
{
    auto* dialog = new brls::Dialog(Tr(title) + "\n\n" + TrMessage(body));
    dialog->addButton(Tr("Close"), [] {});
    dialog->setCancelable(false);
    dialog->open();
}

void LaunchSessionDialog(const GfnClient& client, const AuthSession& auth,
                         const std::string& launch_app_id, const std::string& title,
                         const std::string& launch_store,
                         const std::string& internal_title,
                         const std::string& history_game_id,
                         const std::string& image_url)
{
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setWidth(720);
    box->setPadding(24, 36, 18, 36);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setBackgroundColor(nvgRGB(15, 17, 21));
    box->setCornerRadius(18);

    auto* game_header = new brls::Box(brls::Axis::ROW);
    game_header->setWidth(620);
    game_header->setHeight(96);
    game_header->setAlignItems(brls::AlignItems::CENTER);
    game_header->setMarginBottom(6);

    auto* cover = new brls::Image();
    cover->setWidth(82);
    cover->setHeight(82);
    cover->setCornerRadius(12);
    cover->setScalingType(brls::ImageScalingType::FILL);
    cover->setMarginRight(20);
    SetCachedThumbnailImage(cover, image_url);
    game_header->addView(cover);

    auto* game_copy = new brls::Box(brls::Axis::COLUMN);
    game_copy->setGrow(1.0f);
    auto* eyebrow = new brls::Label();
    eyebrow->setText(Tr("NOW LOADING"));
    eyebrow->setFontSize(13);
    eyebrow->setTextColor(nvgRGB(77, 218, 130));
    eyebrow->setMarginBottom(5);
    game_copy->addView(eyebrow);
    auto* heading = new brls::Label();
    heading->setText(title);
    heading->setFontSize(27);
    heading->setTextColor(nvgRGB(245, 248, 250));
    heading->setMarginBottom(5);
    game_copy->addView(heading);
    auto* store_label = new brls::Label();
    store_label->setText(launch_store.empty() ? "GeForce NOW" : launch_store);
    store_label->setFontSize(14);
    store_label->setTextColor(nvgRGB(145, 151, 162));
    game_copy->addView(store_label);
    game_header->addView(game_copy);
    box->addView(game_header);

    auto* animation = new LaunchAnimationView();
    animation->setMarginBottom(0);
    box->addView(animation);

    auto* stage_label = new brls::Label();
    stage_label->setText(Tr("Checking your NVIDIA account"));
    stage_label->setFontSize(27);
    stage_label->setTextColor(nvgRGB(238, 242, 245));
    stage_label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    stage_label->setMarginBottom(6);
    box->addView(stage_label);

    auto* detail_label = new brls::Label();
    detail_label->setText(Tr("Validating the saved session before requesting a cloud rig."));
    detail_label->setFontSize(16);
    detail_label->setTextColor(nvgRGB(151, 159, 170));
    detail_label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    detail_label->setMarginBottom(4);
    box->addView(detail_label);

    auto ad_playback_state = std::make_shared<AdvertisementPlaybackState>();
    auto* ad_playback_view = new AdvertisementPlaybackView(ad_playback_state);
    ad_playback_view->setVisibility(brls::Visibility::GONE);
    box->addView(ad_playback_view);

    SetLaunchProgress(animation, stage_label, detail_label, 0,
                      "Checking your NVIDIA account",
                      "Validating the saved session before requesting a cloud rig.", 0.08f);

    auto* dialog = new shortcut::ArtworkToastDialog(box);
    dialog->setCancelable(false);

    GfnClient bg_client = client;
    AuthSession bg_auth = auth;
    std::string bg_app_id = launch_app_id;
    std::string bg_store = launch_store;
    std::string bg_internal_title = internal_title.empty() ? title : internal_title;
    std::string bg_history_game_id = history_game_id.empty() ? launch_app_id : history_game_id;

    auto launch_state = std::make_shared<LaunchSessionState>();

    dialog->addButton(Tr("Cancel session"), [launch_state, bg_client, bg_auth]() {
        launch_state->running = false;
        std::string session_id;
        {
            std::lock_guard<std::mutex> lock(launch_state->mutex);
            session_id = launch_state->session_id;
        }
        if (!session_id.empty()) {
            brls::async([bg_client, bg_auth, sid = std::move(session_id)]() mutable {
                try { bg_client.StopSession(bg_auth, sid); } catch (...) {}
            });
        }
    });
    dialog->open();

    brls::async([dialog, game_header, animation, stage_label, detail_label,
                 ad_playback_view, ad_playback_state,
                 bg_client, bg_auth, bg_app_id, bg_store, bg_internal_title,
                 bg_history_game_id, launch_state]() mutable {
        auto post_progress = [animation, stage_label, detail_label, launch_state](
            int stage, std::string title, std::string detail, float progress) {
            brls::sync([animation, stage_label, detail_label, launch_state,
                        stage, title = std::move(title), detail = std::move(detail), progress]() {
                if (!launch_state->running)
                    return;
                SetLaunchProgress(animation, stage_label, detail_label,
                                  stage, title, detail, progress);
            });
        };
        auto show_advertisement = [game_header, animation, stage_label,
                                   detail_label, ad_playback_view,
                                   launch_state](bool show) {
            brls::sync([game_header, animation, stage_label, detail_label,
                        ad_playback_view, launch_state, show]() {
                if (!launch_state->running)
                    return;
                const auto normal = show
                    ? brls::Visibility::GONE : brls::Visibility::VISIBLE;
                game_header->setVisibility(normal);
                animation->setVisibility(normal);
                stage_label->setVisibility(normal);
                detail_label->setVisibility(normal);
                ad_playback_view->setVisibility(show
                    ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            });
        };
        try {
            post_progress(0, opennow::Tr("Checking your NVIDIA account"),
                          opennow::Tr("Renewing authorization and checking previous sessions."), 0.10f);
            SessionInfo info;
            try {
                post_progress(1, opennow::Tr("Requesting a cloud rig"),
                              opennow::Tr("GeForce NOW is allocating hardware for your game."), 0.24f);
                info = bg_client.StartSession(bg_auth, bg_app_id, bg_store, bg_internal_title);
            }
            catch (const ReauthenticationRequired&)
            {
                post_progress(0, opennow::Tr("Signing in automatically"),
                              opennow::Tr("The saved session expired. Using your remembered NVIDIA account."), 0.14f);
                bg_auth = bg_client.ReauthenticateWithSavedCredentials(
                    bg_auth,
                    [post_progress](const std::string& status) {
                        post_progress(0, opennow::Tr("Confirming your NVIDIA account"), status, 0.18f);
                    },
                    [launch_state]() { return !launch_state->running.load(); });
                if (!launch_state->running)
                    return;
                post_progress(1, opennow::Tr("Account restored - requesting a rig"),
                              opennow::Tr("Quick sign-in completed successfully."), 0.26f);
                info = bg_client.StartSession(bg_auth, bg_app_id, bg_store, bg_internal_title);
            }
            {
                std::lock_guard<std::mutex> lock(launch_state->mutex);
                launch_state->session_id = info.session_id;
            }

            int unknown_status_polls = 0;
            bool provider_fallback_used = false;
            int app_patching_retries = 0;
            bool ad_break_logged = false;
            std::unordered_set<std::string> completed_ads;
            ad_report::FailureReports ad_failures;
            HttpClient ad_http_client;

            auto apply_ad_report_response = [&info](SessionInfo updated) {
                // Ad-update replies focus on queue/ad state and may omit the
                // connection data returned by StartSession. Keep that data so
                // the next report and the eventual stream use the same node.
                if (updated.session_id.empty())
                    updated.session_id = info.session_id;
                if (updated.server_ip.empty())
                    updated.server_ip = info.server_ip;
                if (updated.signaling_url.empty())
                    updated.signaling_url = info.signaling_url;
                if (updated.media_ip.empty())
                    updated.media_ip = info.media_ip;
                if (updated.media_port == 0)
                    updated.media_port = info.media_port;
                if (updated.ice_servers.empty())
                    updated.ice_servers = info.ice_servers;
                if (updated.session_token.empty())
                    updated.session_token = info.session_token;
                if (updated.requested_region_url.empty())
                    updated.requested_region_url = info.requested_region_url;
                if (updated.assigned_server_id.empty())
                    updated.assigned_server_id = info.assigned_server_id;
                // Acknowledgements can omit status entirely. Missing queue
                // data stays unknown until the next full poll, never fake zero.
                if (updated.status < 0)
                    updated.status = info.status;
                info = std::move(updated);
            };

            auto report_ad = [&](const SessionAdReport& report,
                                 const char* phase,
                                 int maximum_attempts) -> bool {
                std::string last_error;
                for (int attempt = 1; attempt <= maximum_attempts; ++attempt)
                {
                    if (!launch_state->running)
                        return false;
                    try
                    {
                        const std::string assigned_server = info.server_ip;
                        SessionInfo updated = bg_client.ReportSessionAd(
                            bg_auth, info.session_id, report, assigned_server);
                        apply_ad_report_response(std::move(updated));
                        LogRuntimeEvent(
                            "session", "ad_report_accepted",
                            "phase=" + std::string(phase) +
                                " attempt=" + std::to_string(attempt) +
                                " ad_id=" + report.ad_id +
                                " server=" +
                                (assigned_server.empty()
                                     ? std::string("region")
                                     : assigned_server));
                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        last_error = e.what();
                        const bool recoverable =
                            ad_report::IsRecoverableTransitionError(last_error);
                        LogRuntimeEvent(
                            "session", "ad_report_rejected",
                            "phase=" + std::string(phase) +
                                " attempt=" + std::to_string(attempt) +
                                " recoverable=" +
                                std::to_string(recoverable ? 1 : 0) +
                                " ad_id=" + report.ad_id +
                                " error=" + last_error);
                        if (!recoverable)
                            throw;
                        if (attempt < maximum_attempts)
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(250 * attempt));
                    }
                }

                LogRuntimeEvent(
                    "session", "ad_report_deferred",
                    "phase=" + std::string(phase) +
                        " ad_id=" + report.ad_id +
                        " error=" + last_error);
                return false;
            };
            
            while (info.status != 2 && info.status != 3) {
                if (info.status > 3 && info.status != 6) {
                    throw std::runtime_error("Session ended or aborted by NVIDIA.");
                }

                if (info.status < 0 && ++unknown_status_polls >= 6) {
                    throw std::runtime_error("NVIDIA returned an unknown session status for too long.");
                }
                if (info.status >= 0) unknown_status_polls = 0;
                
                std::string queue_title;
                std::string queue_detail;
                const auto ad_state = ad_break::Classify(info.ad_break);
                if (ad_state == ad_break::PresentationState::MediaReady)
                {
                    const int duration = ad_break::TotalDurationSeconds(info.ad_break);
                    const auto* media = ad_break::SelectPreferredMedia(info.ad_break);
                    queue_title = Tr("Advertisement required");
                    queue_detail = Tr("Free-tier advertisement ready");
                    if (duration > 0)
                        queue_detail += TrFormat(" ({0} seconds)",
                                                 {std::to_string(duration)});
                    if (media && !media->encoding_profile.empty())
                        queue_detail += TrFormat(". Prepared format: {0}",
                                                 {media->encoding_profile});
                    queue_detail += Tr(". Playback will begin automatically.");

                    if (!ad_break_logged)
                    {
                        std::ostringstream details;
                        details << "ads=" << info.ad_break.ads.size()
                                << " duration_seconds=" << duration
                                << " provider=" << info.ad_break.provider
                                << " media_profile="
                                << (media ? media->encoding_profile : "missing");
                        LogRuntimeEvent("session", "ad_break_detected", details.str());
                        ad_break_logged = true;
                    }

                    constexpr size_t kMaximumAdsPerBreak = 16;
                    size_t ads_played_this_break = 0;
                    bool played_ad = false;
                    while (ads_played_this_break < kMaximumAdsPerBreak &&
                           info.status != 2 && info.status != 3)
                    {
                        if (!launch_state->running)
                            return;

                        // NVIDIA may return additional ads after accepting the
                        // Start report, so select from the latest response on
                        // every iteration instead of snapshotting the list.
                        const auto next_ad = std::find_if(
                            info.ad_break.ads.begin(), info.ad_break.ads.end(),
                            [&](const SessionAdInfo& candidate) {
                                return !candidate.id.empty() &&
                                    completed_ads.count(candidate.id) == 0 &&
                                    !ad_failures.Accepted(candidate.id);
                            });
                        if (next_ad == info.ad_break.ads.end())
                            break;
                        const SessionAdInfo ad = *next_ad;
                        const auto* ad_media = ad_break::SelectPreferredMedia(ad);
                        bool start_reported = false;
                        AdvertisementPlaybackResult playback;
                        if (!ad_media || ad_media->url.empty())
                        {
                            playback.error = "NVIDIA did not provide playable advertisement media";
                        }
                        else
                        {
                            show_advertisement(true);
                            playback = PlayAdvertisementMedia(
                                ad, *ad_media, ad_http_client, ad_playback_state,
                                [launch_state]() { return !launch_state->running.load(); },
                                [&]() {
                                    start_reported = report_ad(
                                        ad_report::Started(ad.id), "start", 3);
                                });
                        }

                        if (!playback.completed)
                        {
                            show_advertisement(false);
                            if (!launch_state->running || playback.cancelled)
                            {
                                // User cancellation ends the launch; it is distinct
                                // from a source/load failure during the queue.
                                try {
                                    (void)bg_client.ReportSessionAd(bg_auth, info.session_id,
                                        ad_report::Cancelled(ad.id, playback.watched_time_ms,
                                            "user_cancelled"), info.server_ip);
                                } catch (...) {}
                                return;
                            }
                            LogRuntimeEvent("session", "ad_failure_reporting",
                                "attempt=" + std::to_string(ad_failures.Attempts(ad.id) + 1) +
                                " started=" + std::to_string(playback.started ? 1 : 0) +
                                " watched_ms=" + std::to_string(playback.watched_time_ms) +
                                " error=" + playback.error);
                            const bool accepted = ad_failures.Report(ad.id, playback.watched_time_ms,
                                [&](const SessionAdReport& report) {
                                    return report_ad(report, "cancel_error", 1);
                                });
                            LogRuntimeEvent("session", "ad_failure_resume_polling",
                                "accepted=" + std::to_string(accepted ? 1 : 0) +
                                " state=" + std::to_string(info.status) +
                                " ads_required=" + std::to_string(info.ad_break.required ? 1 : 0));
                            queue_title = Tr("Preparing your cloud rig");
                            queue_detail = Tr("Waiting for NVIDIA after an advertisement loading error.");
                            // Re-poll the existing session; never report Finish
                            // or create a new session for a failed advertisement.
                            break;
                        }

                        // A Start update can race the first queue transition.
                        // Retry it after playback, when the ad opportunity is
                        // definitely active, before sending Finish.
                        if (!start_reported)
                        {
                            start_reported = report_ad(
                                ad_report::Started(ad.id),
                                "start_after_playback", 3);
                        }

                        bool finish_reported = false;
                        if (start_reported)
                        {
                            finish_reported = report_ad(
                                ad_report::Finished(
                                    ad.id, playback.watched_time_ms,
                                    std::max(0, ad.duration_seconds) * 1000),
                                "finish", 3);
                        }

                        // Desktop OpenNOW treats rejected ad transitions as
                        // non-fatal and resumes polling. Do the same instead of
                        // deleting a valid cloud session after the user has
                        // already watched the advertisement.
                        completed_ads.insert(ad.id);
                        ++ads_played_this_break;
                        played_ad = true;
                        LogRuntimeEvent(
                            "session", "ad_playback_completed",
                            "watched_ms=" +
                            std::to_string(playback.watched_time_ms) +
                            " completed=" +
                            std::to_string(completed_ads.size()) +
                            " server_ads=" +
                            std::to_string(info.ad_break.ads.size()) +
                            " start_reported=" +
                            std::to_string(start_reported ? 1 : 0) +
                            " finish_reported=" +
                            std::to_string(finish_reported ? 1 : 0));
                    }
                    if (ads_played_this_break == kMaximumAdsPerBreak)
                        throw std::runtime_error(
                            "NVIDIA returned too many advertisements in one break.");
                    show_advertisement(false);
                    if (played_ad)
                        post_progress(
                            1, opennow::Tr("Advertisement complete"),
                            opennow::Tr("Returning to cloud rig preparation."), 0.48f);
                }
                // Use the latest server state: an ad report may have changed
                // the queue position, ad list, or rig readiness above.
                const auto waiting = ad_break::QueuePresentation(info);
                queue_title = waiting.show_position
                    ? "Position in queue: " + std::to_string(info.queue_position)
                    : waiting.title;
                queue_detail = waiting.show_position
                    ? Tr(waiting.detail) : SessionStatusText(info);
                const bool pending_ad = info.ad_break.required && std::any_of(
                    info.ad_break.ads.begin(), info.ad_break.ads.end(),
                    [&](const SessionAdInfo& ad) {
                        return !ad.id.empty() && completed_ads.count(ad.id) == 0 &&
                            !ad_failures.Accepted(ad.id);
                    });
                if (pending_ad)
                    queue_detail += "\n" + Tr("Advertisement required");
                else if (info.ad_break.required && info.ad_break.ads.empty() &&
                         !ad_failures.HasAccepted())
                    queue_detail += "\n" + Tr("Waiting for NVIDIA to provide the Free-tier advertisement details.");
                if (info.server_redirects)
                    queue_detail += "\n" + Tr("Server reassigned") + ": " + routing::ShortHost(info.server_ip);
                post_progress(1, queue_title, queue_detail,
                              info.status == 0 ? 0.34f : 0.48f);

                if (info.status == 2 || info.status == 3) break;
                const auto next_poll = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(ad_break::QueuePollDelayMs(info));
                while (launch_state->running && std::chrono::steady_clock::now() < next_poll)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!launch_state->running) return;
                try
                {
                    info = bg_client.PollSession(bg_auth, info.session_id);
                }
                catch (const AutomaticRegionSessionLost&)
                {
                    if (provider_fallback_used)
                        throw;

                    provider_fallback_used = true;
                    unknown_status_polls = 0;
                    post_progress(
                        1,
                        opennow::Tr("Trying another server"),
                        opennow::Tr("The fastest region could not keep the cloud session. "
                        "Retrying through NVIDIA's automatic server routing."),
                        0.30f);
                    info = bg_client.StartSession(
                        bg_auth,
                        bg_app_id,
                        bg_store,
                        bg_internal_title,
                        true);
                    {
                        std::lock_guard<std::mutex> lock(
                            launch_state->mutex);
                        launch_state->session_id = info.session_id;
                    }
                }
                catch (const CloudSessionNotFound&)
                {
                    if (!info.app_patching)
                        throw;
                    if (app_patching_retries >= 2)
                    {
                        throw std::runtime_error(
                            "This game is currently being updated on NVIDIA "
                            "servers. SwitchNOW tried again, but no cloud rig "
                            "is available yet. Please try again later.");
                    }

                    ++app_patching_retries;
                    provider_fallback_used = true;
                    unknown_status_polls = 0;
                    post_progress(
                        1,
                        opennow::Tr("Updating the game"),
                        TrFormat(
                            "NVIDIA is patching this title. Waiting before retry {0} of 2.",
                            {std::to_string(app_patching_retries)}),
                        0.42f);

                    for (int wait_step = 0; wait_step < 60; ++wait_step)
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(250));
                        if (!launch_state->running)
                            return;
                    }

                    info = bg_client.StartSession(
                        bg_auth,
                        bg_app_id,
                        bg_store,
                        bg_internal_title,
                        true);
                    {
                        std::lock_guard<std::mutex> lock(
                            launch_state->mutex);
                        launch_state->session_id = info.session_id;
                    }
                }
            }

            if (info.signaling_url.empty()) {
                throw std::runtime_error("Session is ready, but NVIDIA did not return a signaling URL.");
            }

            post_progress(2, opennow::Tr("Connecting to the streaming server"),
                          opennow::Tr("The rig is ready. Configuring the secure network path."), 0.72f);
            brls::sync([=]() {
                if (!launch_state->running) return;
                SetLaunchProgress(animation, stage_label, detail_label, 3,
                                  "Starting the video stream",
                                  "Negotiating WebRTC and waiting for the first clean frame.", 0.86f);
                dialog->close([launch_state](){ launch_state->running = false; });

                auto& app_state = AppState::Instance();
                if (app_state.HasSession() && app_state.session()->user.user_id == bg_auth.user.user_id)
                    app_state.SetSession(bg_auth);
                const std::string played_at = CurrentUtcIsoTimestamp();
                RecordGamePlayed(bg_history_game_id, bg_internal_title, played_at);
                app_state.MarkGamePlayed(bg_history_game_id, bg_internal_title, played_at);
                
                brls::Logger::info("WebRTC Stream Ready!");
                brls::Logger::info("Server IP: {}", info.server_ip);
                brls::Logger::info("Signaling URL: {}", info.signaling_url);
                brls::Logger::info("Media endpoint: {}:{}", info.media_ip, info.media_port);
                brls::Logger::info("ICE servers: {}", info.ice_servers.size());
                
                std::string jwt_token = bg_auth.tokens.id_token.empty() ? bg_auth.tokens.access_token : bg_auth.tokens.id_token;
                brls::Application::pushActivity(new brls::Activity(StreamView::create(
                    info.signaling_url,
                    jwt_token,
                    info.session_id,
                    info.media_ip,
                    info.media_port,
                    info.ice_servers,
                    bg_client,
                    bg_auth,
                    bg_internal_title, info.requested_region_url, info.server_ip, false,
                    info.initial_server_ip, info.server_redirects, info.cloud_client_profile,bg_store)));
            });

        } catch (const std::exception& e) {
            show_advertisement(false);
            const auto presentation = session_error::Present(e.what());
            brls::sync([=]() {
                if (!launch_state->running) return;
                animation->SetState(0, 0.04f);
                stage_label->setText(Tr(presentation.title));
                stage_label->setTextColor(nvgRGB(255, 125, 132));
                detail_label->setText(TrMessage(presentation.body) + "\n\n" +
                                      Tr("Press B to return to the game page."));
                dialog->setCancelable(true);
            });
        }
    });
}

} // namespace opennow
