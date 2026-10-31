#include "main_tabs_view.hpp"

#include "app_state.hpp"
#include "catalog_tab.hpp"
#include "gfn_client.hpp"
#include "library_tab.hpp"
#include "settings_tab.hpp"
#include "localization.hpp"

#include <exception>
#include <chrono>
#include <utility>

namespace opennow
{

MainTabsView::MainTabsView()
{
    bool automatic_sign_in_needed = false;
    auto& state = AppState::Instance();
    if (!state.IsSessionLoaded())
    {
        AuthSession session;
        GfnClient client;
        if (client.LoadSavedSession(session))
        {
            try
            {
                session = client.EnsureFreshSavedSession(session);
            }
            catch (const ReauthenticationRequired&)
            {
                session.reauthentication_required = true;
                automatic_sign_in_needed =
                    client.LoadNativeCredentials(session.provider.idp_id).has_value();
                brls::Application::notify(automatic_sign_in_needed
                    ? opennow::Tr("NVIDIA session expired; quick sign-in will start automatically")
                    : opennow::Tr("NVIDIA requires this account to sign in again"));
            }
            catch (const std::exception&)
            {
                // Keep the saved account during temporary network failures.
                brls::Application::notify(opennow::Tr("Saved account loaded; automatic renewal will retry when online"));
            }
            state.SetSession(std::move(session));
        }
        else
            state.MarkSessionLoaded();
    }
    else if (state.HasSession() && state.session()->reauthentication_required)
    {
        GfnClient client;
        automatic_sign_in_needed =
            client.LoadNativeCredentials(state.session()->provider.idp_id).has_value();
    }

    addTab("Store", []() { return new CatalogTab(); });
    addTab("Library", []() { return new LibraryTab(); });
    addTab("Settings", []() { return new SettingsTab(); });
    focusTab(1);
    last_auth_check_ = automatic_sign_in_needed
        ? std::chrono::steady_clock::time_point {}
        : std::chrono::steady_clock::now();
}

void MainTabsView::MaybeRefreshAuthentication()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - last_auth_check_ < std::chrono::minutes(1))
        return;
    last_auth_check_ = now;

    auto& state = AppState::Instance();
    if (!state.HasSession())
        return;

    const AuthSession session = *state.session();
    const bool automatic_sign_in = session.reauthentication_required;
    if (automatic_sign_in)
    {
        if (now < auto_reauth_backoff_until_)
            return;
        GfnClient credential_probe;
        if (!credential_probe.LoadNativeCredentials(session.provider.idp_id))
            return;
    }

    if (auth_refresh_running_->exchange(true))
        return;
    const auto guard = auth_refresh_running_;
    if (automatic_sign_in)
        brls::Application::notify(opennow::Tr("Signing in to NVIDIA automatically..."));
    brls::async([this, session, guard, automatic_sign_in]() mutable {
        try
        {
            GfnClient client;
            AuthSession refreshed = client.RecoverSavedSession(session);
            brls::sync([this, refreshed = std::move(refreshed), guard, automatic_sign_in]() mutable {
                auto& current = AppState::Instance();
                if (current.HasSession() &&
                    current.session()->user.user_id == refreshed.user.user_id)
                {
                    current.SetSession(std::move(refreshed));
                    auto_reauth_backoff_until_ = {};
                    if (automatic_sign_in)
                        brls::Application::notify(opennow::Tr("NVIDIA quick sign-in completed"));
                }
                guard->store(false);
            });
        }
        catch (const ReauthenticationRequired&)
        {
            brls::sync([this, session, guard]() mutable {
                auto& current = AppState::Instance();
                if (current.HasSession() && current.session()->user.user_id == session.user.user_id)
                {
                    AuthSession invalid = *current.session();
                    invalid.reauthentication_required = true;
                    current.SetSession(std::move(invalid));
                    last_auth_check_ = {};
                    GfnClient credential_probe;
                    brls::Application::notify(
                        credential_probe.LoadNativeCredentials(session.provider.idp_id)
                            ? opennow::Tr("NVIDIA session expired; quick sign-in is starting automatically")
                            : opennow::Tr("NVIDIA requires this account to sign in again"));
                }
                guard->store(false);
            });
        }
        catch (const std::exception&)
        {
            brls::sync([this, guard, automatic_sign_in]() {
                if (automatic_sign_in)
                {
                    auto_reauth_backoff_until_ =
                        std::chrono::steady_clock::now() + std::chrono::seconds(30);
                    brls::Application::notify(
                        opennow::Tr("Automatic NVIDIA sign-in needs confirmation; open Library to continue"));
                }
                guard->store(false);
            });
        }
    });
}

void MainTabsView::draw(NVGcontext* vg, float x, float y, float width, float height,
                        brls::Style style, brls::FrameContext* ctx)
{
    MaybeRefreshAuthentication();
    TopBarFrame::draw(vg, x, y, width, height, style, ctx);
}

} // namespace opennow
