#include "ui_helpers.hpp"
#include "StreamView.hpp"
#include "app_state.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"
#include <atomic>
#include <memory>

namespace opennow {
void ResumeSavedSessionDialog(const GfnClient& client, const AuthSession& auth) {
    static std::weak_ptr<std::atomic<bool>> active_job;
    if (const auto existing = active_job.lock(); existing && existing->load()) return;
    const auto saved = client.LoadResumableCloudSession(auth);
    if (!saved) {
        ShowDialog(opennow::Tr("Continue active session"), opennow::Tr("No saved game session for this account"));
        return;
    }
    // B is disabled; the explicit Cancel button marks the job invalid before
    // the dialog can be destroyed. Late responses cannot open another activity.
    auto running = std::make_shared<std::atomic<bool>>(true);
    active_job = running;
    auto* dialog = new brls::Dialog(Tr("Checking your existing cloud session"));
    dialog->setCancelable(false);
    dialog->addButton(Tr("Cancel"), [running] { running->store(false); });
    dialog->open();
    brls::async([client, auth, saved, running, dialog]() mutable {
        try {
            auto current = auth;
            const auto info = client.ReconnectSession(current, saved->session_id,
                [running] { return !running->load(); }, true);
            brls::sync([client, current, saved, info, running, dialog] {
                if (!running->exchange(false)) return;
                auto& app = AppState::Instance();
                if (!app.HasSession() || app.session()->user.user_id != current.user.user_id ||
                    app.session()->provider.idp_id != current.provider.idp_id ||
                    app.session()->provider.streaming_service_url != current.provider.streaming_service_url) {
                    dialog->close(); return;
                }
                app.SetSession(current);
                dialog->close();
                const auto jwt = current.tokens.id_token.empty()
                    ? current.tokens.access_token : current.tokens.id_token;
                LogRuntimeEvent("recovery", "manual_resume");
                brls::Application::pushActivity(new brls::Activity(StreamView::create(
                    info.signaling_url, jwt, info.session_id, info.media_ip, info.media_port,
                    info.ice_servers, client, current, saved->title,
                    info.requested_region_url, info.server_ip, true,
                    info.initial_server_ip, info.server_redirects, info.cloud_client_profile,saved->store)));
            });
        } catch (...) {
            brls::sync([running, dialog] {
                if (!running->exchange(false)) return;
                dialog->close();
                ShowError(opennow::Tr("Continue active session"), opennow::Tr("The cloud session could not be resumed. Check your connection and account. The server may have ended the game."));
            });
        }
    });
}
} // namespace opennow
