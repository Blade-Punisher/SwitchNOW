#pragma once

#include "http_client.hpp"
#include "models.hpp"

#include <functional>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <vector>

namespace opennow
{

class ReauthenticationRequired : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class NativeLoginFallbackRequired : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class CloudSessionNotFound : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class AutomaticRegionSessionLost : public CloudSessionNotFound
{
  public:
    using CloudSessionNotFound::CloudSessionNotFound;
};

class GfnClient
{
  public:
    GfnClient();

    std::vector<LoginProvider> FetchLoginProviders() const;
    std::vector<PublicGame> FetchPublicGames() const;
    std::vector<PublicGame> FetchStoreGames(AuthSession* session, const std::string& query = {},
        const std::function<void(std::vector<PublicGame>, bool)>& snapshot = {}, bool force = false) const;
    std::vector<PublicGame> FetchShortcutArtwork(const AuthSession& session,
        const std::string& game_id) const;
    std::vector<PublicGame> FetchCatalogGames(
        AuthSession& session, const std::string& search_query = {},
        const std::function<void(std::vector<PublicGame>, bool)>& snapshot = {}) const;
    void MarkGameUnowned(AuthSession& session,const std::string& app_id,const std::string& variant_id) const;
    void MarkGameOwned(AuthSession& session,const std::string& app_id,const std::string& variant_id) const;
    std::vector<GameInfo> FetchLibraryGames(AuthSession& session, bool* complete = nullptr,
        const std::function<void(std::vector<GameInfo>, bool)>& snapshot = {}, bool force_full = false) const;
    std::vector<StreamRegion> FetchStreamRegions(AuthSession& session) const;
    std::vector<StreamRegion> MeasureStreamRegionLatencies(
        std::vector<StreamRegion> regions, const std::string& provider_url) const;
    std::vector<StreamRegion> LoadCachedStreamRegions(
        const AuthSession& session) const;
    void SaveStreamRegionMeasurements(
        const AuthSession& session,
        const std::vector<StreamRegion>& regions) const;
    void RecordObservedStreamRtt(
        const AuthSession& session, const std::string& selected_region,
        const std::string& actual_server, const std::string& media_endpoint,
        int rtt_ms, int p95_ms, int transport_pauses,
        int transport_pause_ms, int session_seconds,
        int playback_pauses, int playback_pause_ms, const std::string& initial_server = {},
        const std::string& cloud_session_id = {}, int low_display_ms = 0, int audio_underruns = 0) const;

    AuthSession Login(const LoginProvider& provider, const std::string& login_hint = {}) const;
    AuthSession LoginNative(
        const LoginProvider& provider,
        const std::string& email,
        const std::string& password,
        const std::function<std::string(const std::string&)>& request_one_time_code,
        const std::function<void(const std::string&)>& report_status = {},
        const std::function<bool()>& is_cancelled = {}) const;
    AuthSession ReauthenticateWithSavedCredentials(
        const AuthSession& expired_session,
        const std::function<void(const std::string&)>& report_status = {},
        const std::function<bool()>& is_cancelled = {}) const;
    AuthSession LoginSwitchQR(const LoginProvider& provider, std::function<void(const std::string&)> onUrlGenerated, std::function<bool()> isCancelled) const;
    AuthSession EnsureFreshSession(const AuthSession& session) const;
    AuthSession EnsureFreshSavedSession(const AuthSession& session) const;
    AuthSession ForceRefreshSavedSession(const AuthSession& session) const;
    AuthSession RecoverSavedSession(
        const AuthSession& session,
        bool force_refresh = false,
        const std::function<void(const std::string&)>& report_status = {},
        const std::function<bool()>& is_cancelled = {}) const;

    SessionInfo StartSession(AuthSession& session, const std::string& launch_app_id,
                             const std::string& launch_store = "",
                             const std::string& internal_title = "",
                             bool force_provider_endpoint = false) const;
    SessionInfo PollSession(AuthSession& session, const std::string& session_id,
        const std::function<bool()>& is_cancelled = {}, long timeout_seconds = 30) const;
    SessionInfo ReconnectSession(AuthSession& session, const std::string& session_id,
        const std::function<bool()>& is_cancelled = {}, bool manual_claim = false,
        const std::string& failed_signaling_endpoint = {}) const;
    std::optional<ResumableCloudSession> LoadResumableCloudSession(const AuthSession& auth) const;
    // Returns the original stream age; reconnect must not restart the Free timer.
    std::chrono::seconds MarkCloudSessionStreaming(const AuthSession& auth,
        const std::string& session_id, const std::string& title,
        const std::string& server = {}, const std::string& store = {}) const;
    SessionInfo ReportSessionAd(
        AuthSession& session,
        const std::string& session_id,
        const SessionAdReport& report,
        const std::string& server_ip = "") const;
    void StopSession(
        AuthSession& session,
        const std::string& session_id,
        const std::string& server_ip = "") const;
    void CleanupStaleCloudSession(AuthSession& session) const;

    bool LoadSavedSession(AuthSession& session) const;
    std::vector<AuthSession> LoadSavedSessions() const;
    void SaveSession(const AuthSession& session) const;
    bool SetActiveSavedSession(const std::string& user_id) const;
    void ClearSavedSession() const;
    void ClearAllSavedSessions() const;
    std::optional<NativeCredentials> LoadNativeCredentials(const std::string& provider_id) const;
    void SaveNativeCredentials(const NativeCredentials& credentials) const;
    void ClearNativeCredentials(const std::string& provider_id) const;
    void ClearAllNativeCredentials() const;
    std::string LoadLauncherPreference(const std::string& user_id, const std::string& game_id) const;
    void SaveLauncherPreference(const std::string& user_id, const std::string& game_id,
                                const std::string& variant_id) const;

    static constexpr const char* kUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36 "
        "NVIDIACEFClient/HEAD/debb5919f6 GFN-PC/2.0.80.173";

  private:
    void SetGameOwnership(AuthSession& session,const std::string& app_id,const std::string& variant_id,bool owned) const;
    HttpClient http_client_;
    std::string client_id_ = "d54a2a22-386f-4a30-84dc-6680a6b7d188";
    std::string device_id_;
};

} // namespace opennow
