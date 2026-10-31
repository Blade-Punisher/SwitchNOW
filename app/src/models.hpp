#pragma once
#include "gfn/cloud_client_profile.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opennow
{

struct LoginProvider
{
    std::string idp_id;
    std::string code;
    std::string display_name;
    std::string streaming_service_url;
    int priority = 0;
};

struct AuthTokens
{
    std::string access_token;
    std::string refresh_token;
    std::string id_token;
    std::string client_token;
    std::int64_t expires_at_ms              = 0;
    std::int64_t client_token_expires_at_ms = 0;
};

struct AuthUser
{
    std::string user_id;
    std::string display_name;
    std::string email;
    std::string avatar_url;
    std::string membership_tier;
    bool membership_tier_verified = false;
};

struct SubscriptionInfo
{
    std::string membership_tier;
    std::string subscription_type;
    std::string subscription_subtype;
    double allotted_hours = 0.0;
    double purchased_hours = 0.0;
    double rolled_over_hours = 0.0;
    double used_hours = 0.0;
    double remaining_hours = 0.0;
    double total_hours = 0.0;
    bool is_unlimited = false;
    bool available = false;
    bool has_storage = false;
    bool has_storage_usage = false;
    double storage_size_gb = 0.0;
    double storage_used_gb = 0.0;
    std::string storage_region_name;
    std::string storage_region_code;
};

struct AuthSession
{
    LoginProvider provider;
    AuthTokens tokens;
    AuthUser user;
    SubscriptionInfo subscription;
    std::int64_t last_refresh_at_ms = 0;
    std::int64_t membership_checked_at_ms = 0;
    bool persistence_enabled = true; // Runtime-only; vault sessions are persistent.
    bool reauthentication_required = false; // Runtime-only; set after invalid_grant/401 refresh failure.
};

struct NativeCredentials
{
    std::string provider_id;
    std::string email;
    std::string password;
};

struct ResumableCloudSession {
    std::string server;
    std::string session_id;
    std::string launch_app_id;
    std::string title;
    std::int64_t started_at_ms = 0;
    int width = 0, height = 0, fps = 0;
    std::string codec;
    std::string store;
};

struct GameVariant
{
    std::string id;
    std::string store;
    std::string library_status;
    std::string last_played_date;
    std::string gfn_status;
    bool library_selected = false;
};

struct PublicGame
{
    std::string id;
    std::string uuid;
    std::string launch_app_id;
    std::string title;
    std::string store;
    std::string publisher;
    std::string developer;
    std::string description;
    std::string image_url;
    std::string poster_url;
    std::string hero_url;
    std::vector<std::string> screenshots;
    std::vector<std::string> genres;
    std::vector<std::string> feature_labels;
    std::vector<std::string> content_ratings;
    std::vector<std::string> supported_controls;
    std::string membership_tier_label;
    bool is_in_library = false;
    std::vector<GameVariant> variants;
};

struct GameInfo
{
    std::string id;
    std::string uuid;
    std::string launch_app_id;
    std::string title;
    std::string description;
    std::string publisher;
    std::string developer;
    std::string image_url;
    std::string poster_url;
    std::string hero_url;
    std::vector<std::string> screenshots;
    std::vector<std::string> genres;
    std::vector<std::string> feature_labels;
    std::vector<std::string> content_ratings;
    std::vector<std::string> supported_controls;
    std::string membership_tier_label;
    std::string last_played;
    bool is_in_library          = false;
    size_t selected_variant_index = 0;
    std::vector<std::string> available_stores;
    std::vector<GameVariant> variants;
};

struct IceServerInfo
{
    std::string url;
    std::string username;
    std::string credential;
};

struct AdMediaFileInfo
{
    std::string url;
    std::string encoding_profile;
};

struct SessionAdInfo
{
    std::string id;
    int state = 0;
    int duration_seconds = 0;
    std::vector<AdMediaFileInfo> media_files;
};

struct AdBreakInfo
{
    bool required = false;
    int provider = 0;
    std::vector<SessionAdInfo> ads;
};

enum class SessionAdAction
{
    Start = 1,
    Pause = 2,
    Resume = 3,
    Finish = 4,
    Cancel = 5,
};

struct SessionAdReport
{
    std::string ad_id;
    SessionAdAction action = SessionAdAction::Start;
    int watched_time_ms = -1;
    int paused_time_ms = -1;
    std::string cancel_reason;
};

struct StreamRegion
{
    std::string name;
    std::string url;
    int ping_ms = -1;
    int jitter_ms = -1;
    int selection_score_ms = -1;
    int successful_probes = 0;
    int attempted_probes = 0;
    bool probe_degraded = false;
    int observed_stream_rtt_ms = -1;
    int observed_stream_p95_ms = -1;
    int observed_transport_pause_count = 0;
    int observed_transport_pause_ms = 0;
    int observed_playback_pause_count = 0;
    int observed_playback_pause_ms = 0;
    int observed_session_seconds = 0;
    std::int64_t observed_at_ms = 0;
    std::string observed_server_host;
    std::string observed_media_endpoint;
    // Derived from recent, individual requested-region -> actual-node sessions.
    int route_history_penalty_ms = 0;
    int route_history_samples = 0;
    int route_history_redirects = 0;
    int route_history_expected_rtt_ms = -1;
    std::int64_t route_history_at_ms = 0;
    std::string zone_id;
    int zone_queue_estimate = -1;
    std::int64_t zone_queue_at_ms = 0;
    std::int64_t zone_catalog_at_ms = 0;
};

struct SessionInfo
{
    std::string session_id;
    int status = -1;
    int queue_position = -1; // -1: unknown/missing; 0: explicitly no position
    std::string queue_source = "missing";
    bool queue_conflict = false;
    int seat_setup_step = -1;
    bool app_patching = false;
    
    // WebRTC / GFN Streaming details
    std::string session_token;
    std::string server_ip;
    // Control routing is distinct from the regional node and media endpoint.
    std::string control_ip;
    std::string signaling_url;
    gfn::cloud_session::ClientProfile cloud_client_profile = gfn::cloud_session::ClientProfile::NativeWebRtc;
    std::vector<std::string> signaling_candidates;
    std::string media_ip;
    std::string requested_region_url;
    std::string assigned_server_id;
    std::string initial_server_ip;
    unsigned server_redirects = 0;
    int media_port = 0;
    std::vector<IceServerInfo> ice_servers;
    AdBreakInfo ad_break;
};

} // namespace opennow
