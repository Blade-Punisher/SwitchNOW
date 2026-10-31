#include "main_activity.hpp"

#include "app_state.hpp"
#include "gfn_client.hpp"
#include "game_detail_view.hpp"
#include "home_shortcut.hpp"
#include "shortcut_worker.hpp"
#include "home_artwork_toast.hpp"
#include "main_tabs_view.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"
#include "ui_helpers.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace opennow
{
namespace
{

class StartupPulseView final : public brls::View
{
  public:
    StartupPulseView()
    {
        setWidth(76);
        setHeight(76);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override
    {
        (void)style;
        (void)ctx;
        constexpr float kPi = 3.14159265358979323846f;
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const float cx = x + width * 0.5f;
        const float cy = y + height * 0.5f;
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(seconds * 3.0));

        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 28.0f + pulse * 4.0f);
        nvgFillColor(vg, nvgRGBA(77, 218, 130, 18 + static_cast<int>(pulse * 22.0f)));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 21.0f);
        nvgStrokeWidth(vg, 5.0f);
        nvgStrokeColor(vg, nvgRGB(35, 43, 48));
        nvgStroke(vg);

        const float angle = static_cast<float>(
            std::fmod(seconds * 3.2, static_cast<double>(kPi) * 2.0));
        nvgBeginPath(vg);
        nvgArc(vg, cx, cy, 21.0f, angle, angle + kPi * 1.42f, NVG_CW);
        nvgStrokeWidth(vg, 5.0f);
        nvgLineCap(vg, NVG_ROUND);
        nvgStrokeColor(vg, nvgRGB(77, 218, 130));
        nvgStroke(vg);

        const float head_angle = angle + kPi * 1.42f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx + std::cos(head_angle) * 21.0f,
                  cy + std::sin(head_angle) * 21.0f, 3.4f);
        nvgFillColor(vg, nvgRGB(123, 242, 166));
        nvgFill(vg);
    }
};

class StartupGateView final : public brls::Box
{
  public:
    explicit StartupGateView(
        std::optional<shortcut::LaunchRequest> launch_request,
        std::optional<shortcut::InstallerResult> installer_result)
        : brls::Box(brls::Axis::COLUMN)
        , launch_request_(std::move(launch_request))
        , installer_result_(std::move(installer_result))
    {
        setGrow(1.0f);
        setAlignItems(brls::AlignItems::CENTER);
        setJustifyContent(brls::JustifyContent::CENTER);
        setBackgroundColor(nvgRGB(10, 12, 15));

        GfnClient client;
        AuthSession saved;
        if (!client.LoadSavedSession(saved))
        {
            LogRuntimeEvent("auth", "startup_no_saved_session");
            AppState::Instance().MarkSessionLoaded();
            ShowMainTabs();
            return;
        }

        LogRuntimeEvent("auth", "startup_saved_session_found");
        if (installer_result_)
        {
            // Restore the already running game page immediately after handoff.
            // Play performs its normal session freshness check when selected.
            AppState::Instance().SetSession(std::move(saved));
            ShowMainTabs();
            return;
        }
        BuildStatusView(saved);
        BeginAuthentication(std::move(client), std::move(saved));
    }

    ~StartupGateView() override
    {
        alive_->store(false);
    }

  private:
    void BuildStatusView(const AuthSession& saved)
    {
        auto* brand = new brls::Label();
        brand->setText("SwitchNOW");
        brand->setFontSize(30);
        brand->setTextColor(nvgRGB(77, 218, 130));
        brand->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        brand->setMarginBottom(34);
        addView(brand);

        auto* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(620);
        card->setPadding(28, 38, 30, 38);
        card->setAlignItems(brls::AlignItems::CENTER);
        card->setBackgroundColor(nvgRGB(18, 21, 25));
        card->setCornerRadius(18);

        auto* eyebrow = new brls::Label();
        eyebrow->setText(Tr("SAVED NVIDIA ACCOUNT"));
        eyebrow->setFontSize(13);
        eyebrow->setTextColor(nvgRGB(77, 218, 130));
        eyebrow->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        eyebrow->setMarginBottom(8);
        card->addView(eyebrow);

        auto* account = new brls::Label();
        account->setText(saved.user.display_name.empty() ? saved.user.email : saved.user.display_name);
        account->setFontSize(25);
        account->setTextColor(nvgRGB(242, 246, 248));
        account->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        account->setMarginBottom(14);
        card->addView(account);

        card->addView(new StartupPulseView());

        status_label_ = new brls::Label();
        status_label_->setText(Tr("Checking saved sign-in"));
        status_label_->setFontSize(21);
        status_label_->setTextColor(nvgRGB(238, 242, 245));
        status_label_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        status_label_->setMarginTop(8);
        status_label_->setMarginBottom(7);
        card->addView(status_label_);

        detail_label_ = new brls::Label();
        detail_label_->setText(Tr("Validating the session before loading your library."));
        detail_label_->setFontSize(15);
        detail_label_->setTextColor(nvgRGB(145, 153, 164));
        detail_label_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        card->addView(detail_label_);
        addView(card);
    }

    void SetStatus(std::string title, std::string detail)
    {
        const auto alive = alive_;
        brls::sync([this, alive, title = std::move(title), detail = std::move(detail)]() {
            if (!alive->load() || finished_)
                return;
            status_label_->setText(Tr(title));
            detail_label_->setText(TrMessage(detail));
        });
    }

    void Complete(AuthSession session, const std::string& notification)
    {
        const auto alive = alive_;
        brls::sync([this, alive, session = std::move(session), notification]() mutable {
            if (!alive->load() || finished_)
                return;
            AppState::Instance().SetSession(std::move(session));
            ShowMainTabs();
            if (!notification.empty())
                brls::Application::notify(Tr(notification));
        });
    }

    void BeginAuthentication(GfnClient client, AuthSession saved)
    {
        const auto alive = alive_;
        const std::uint64_t operation_id =
            BeginRuntimeOperation("auth", "startup_session_recovery");
        brls::async([this, alive, operation_id, client = std::move(client),
                     saved = std::move(saved)]() mutable {
            try
            {
                SetStatus(opennow::Tr("Checking NVIDIA session"),
                          opennow::Tr("Refreshing authorization and verifying account access."));
                AuthSession verified = client.RecoverSavedSession(
                    saved,
                    false,
                    [this](const std::string& status) {
                        SetStatus(opennow::Tr("Signing in automatically"), status);
                    },
                    [alive]() { return !alive->load(); });

                if (!alive->load())
                {
                    EndRuntimeOperation(
                        operation_id, "auth", "startup_session_recovery",
                        "cancelled");
                    return;
                }
                SetStatus(opennow::Tr("Account verified"), opennow::Tr("Loading SwitchNOW and your saved profile."));
                verified.reauthentication_required = false;
                EndRuntimeOperation(
                    operation_id, "auth", "startup_session_recovery", "ok");
                Complete(std::move(verified), "NVIDIA account is ready");
            }
            catch (const ReauthenticationRequired& ex)
            {
                if (!alive->load())
                {
                    EndRuntimeOperation(
                        operation_id, "auth", "startup_session_recovery",
                        "cancelled");
                    return;
                }
                saved.reauthentication_required = true;
                EndRuntimeOperation(
                    operation_id, "auth", "startup_session_recovery",
                    "reauthentication_required", ex.what());
                Complete(std::move(saved),
                         "Automatic sign-in needs the saved password or account confirmation");
                brls::Logger::warning("Startup automatic sign-in requires interaction: {}", ex.what());
            }
            catch (const std::exception& ex)
            {
                if (!alive->load())
                {
                    EndRuntimeOperation(
                        operation_id, "auth", "startup_session_recovery",
                        "cancelled");
                    return;
                }
                const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                const bool expired = saved.tokens.expires_at_ms > 0 &&
                    saved.tokens.expires_at_ms <= now_ms;
                saved.reauthentication_required = saved.reauthentication_required || expired;
                EndRuntimeOperation(
                    operation_id, "auth", "startup_session_recovery",
                    expired ? "expired" : "temporary_error", ex.what());
                Complete(std::move(saved),
                         expired
                            ? "Automatic sign-in could not finish; it will retry in SwitchNOW"
                            : "Session check is temporarily unavailable; using the saved account");
                brls::Logger::warning("Startup authentication check failed: {}", ex.what());
            }
        }, false);
    }

    void ShowMainTabs()
    {
        if (finished_)
            return;
        finished_ = true;
        clearViews();
        setAlignItems(brls::AlignItems::STRETCH);
        setJustifyContent(brls::JustifyContent::FLEX_START);
        setPadding(0);
        auto* tabs = new MainTabsView();
        // Borealis lays activities out in a logical 1280x720 coordinate space.
        // windowWidth/windowHeight are the physical framebuffer dimensions and
        // become 1920x1080 while docked. Applying them here scales the tab tree
        // twice, allowing cards and scroll targets to extend beyond the TV.
        // Flex sizing keeps the same visible bounds across dock/undock changes.
        tabs->setGrow(1.0f);
        addView(tabs);
        invalidate();
        brls::Application::giveFocus(tabs);
        const auto alive = alive_;
        brls::sync([this, alive] {
            if (!alive->load()) return;
            if (installer_result_)
            {
                auto result = std::move(*installer_result_);
                installer_result_.reset();
                if (!RestoreShortcutGameDetail(result))
                    ShowDialog(result.success ? Tr("HOME shortcut created")
                                              : Tr("HOME shortcut installation failed"),
                               result.success ? result.title : result.error);
                return;
            }
            MaybeLaunchShortcut();
        });
    }

    void MaybeLaunchShortcut()
    {
        if (!launch_request_)
            return;

        shortcut::LaunchRequest request = std::move(*launch_request_);
        launch_request_.reset();
        const auto& session = AppState::Instance().session();
        if (!session)
        {
            ShowError(
                Tr("Shortcut needs sign-in"),
                Tr("Connect a GeForce NOW account in SwitchNOW, then open the game shortcut again."));
            return;
        }
        if (session->reauthentication_required)
        {
            ShowError(
                Tr("Shortcut needs sign-in"),
                Tr("Reconnect the saved GeForce NOW account, then open the game shortcut again."));
            return;
        }

        LogRuntimeEvent(
            "shortcut", "direct_launch",
            "title=" + request.title + " launch_app_id=" + request.launch_app_id);
        // Start the game first. The artwork task never owns the launch flow.
        GfnClient client;
        LaunchSessionDialog(client, *session, request.launch_app_id, request.title,
            request.store, request.title, request.game_id, request.image_url);
        if (!request.manifest_path.empty())
        {
            shortcut::RunTask([request, session = *session]() mutable {
                if (shortcut::RefreshGameShortcut(request, session))
                    brls::sync([] { shortcut::ShowHomeArtworkToast(); });
            });
        }
    }

    brls::Label* status_label_ = nullptr;
    brls::Label* detail_label_ = nullptr;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    std::optional<shortcut::LaunchRequest> launch_request_;
    std::optional<shortcut::InstallerResult> installer_result_;
    bool finished_ = false;
};

} // namespace

MainActivity::MainActivity(
    std::optional<shortcut::LaunchRequest> launch_request,
    std::optional<shortcut::InstallerResult> installer_result)
    : brls::Activity(new StartupGateView(std::move(launch_request), std::move(installer_result)))
{
}

} // namespace opennow
