// CloudMatch recovery is intentionally separate from StartSession: it never
// creates a rig, changes its video configuration or cleans up an active game.
SessionInfo GfnClient::ReconnectSession(AuthSession& auth, const std::string& id,
    const std::function<bool()>& cancelled, bool manual_claim, const std::string& failed_signaling_endpoint) const
{
    if (id.empty() || auth.user.user_id.empty()) throw CloudSessionNotFound("No active session to reconnect");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    const auto abort = [&] {
        return (cancelled && cancelled()) || std::chrono::steady_clock::now() >= deadline;
    };
    HttpCancellationScope cancellation_scope(abort);
    // Validate the saved routing identity before touching CloudMatch. Never
    // fall back to a different session with the same game title.
    JsonPtr saved(nullptr, &json_decref);
    {
        std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
        saved = LoadJson(ReadTextFile(GetActiveCloudSessionPath()));
    }
    if (GetString(saved.get(), "session_id") != id || !recovery::Matches(
        {GetString(saved.get(), "user_id"), GetString(saved.get(), "provider_id"),
         GetString(saved.get(), "provider_url"), GetString(saved.get(), "device_id"),
         GetString(saved.get(), "client_id")},
        {auth.user.user_id, auth.provider.idp_id, auth.provider.streaming_service_url, device_id_, client_id_}))
        throw CloudSessionNotFound("The active session belongs to a different account or provider");
    const auto client_profile = gfn::cloud_session::ReadClientProfile(saved.get());
    bool claimed = false;
    int previous_status = -999;
    while (!abort()) {
        auto info = PollSession(auth, id, abort, 8);
        if (abort()) break;
        const bool endpoints = !info.signaling_url.empty() && !info.media_ip.empty() &&
            info.media_port > 0 && info.media_port <= 65535;
        const auto state = recovery::ClassifyCloudState(info.status, endpoints);
        if (info.status != previous_status) {
            previous_status = info.status;
            AppendSessionTraceLog("RECOVERY state status=" + std::to_string(info.status) +
                " claimed=" + std::to_string(claimed) + " endpoints=" + std::to_string(endpoints));
            routing::Event(GetAppHome(), "recovery_state", {{"cloud_session", id},
                {"status", std::to_string(info.status)}, {"claimed", std::to_string(claimed)},
                {"server_host", routing::Host(info.server_ip)}, {"media_host", routing::Host(info.media_ip)}});
        }
        if (state == recovery::CloudState::Ended)
            throw CloudSessionNotFound("The cloud session has ended");
        if (manual_claim && !claimed && (info.status == 2 || info.status == 3)) {
            const auto sub_id = gfn::cloud_session::SubSessionIdFromRandom(GenerateRandomBytes(16));
            const auto body = gfn::cloud_session::BuildResumeBody(
                GetString(saved.get(), "launch_app_id"), device_id_, sub_id, client_profile);
            std::string base = LoadActiveCloudSessionBaseUrl(auth, id);
            // Official CloudMatch rigs expose their own control endpoint;
            // alliance providers retain the saved provider routing contract.
            if ((auth.provider.code == "NVIDIA" ||
                 auth.provider.streaming_service_url.find("nvidiagrid.net") != std::string::npos) &&
                server_zone::IsDirectControlHost(server_zone::ControlHost(info)))
                base = "https://" + server_zone::ControlHost(info) + "/";
            std::vector<std::string> headers = gfn::cloud_session::BuildSessionHeaders(
                ResolveSessionJwt(auth),client_id_,device_id_,client_profile);
            AppendSessionTraceLog("ROUTING event=resume_profile profile="+
                std::string(gfn::cloud_session::ProfileName(client_profile)));

            auto response = http_client_.Request("PUT", base + "v2/session/" + id,
                kUserAgent, headers, body, 5, 8, 1024 * 1024,
                proxy::EnabledUrl(LoadStreamSettings()), abort);
            if (response.status_code == 401 && !abort()) {
                auth = ForceRefreshSavedSession(auth);
                headers[0] = "Authorization: GFNJWT " + ResolveSessionJwt(auth);
                response = http_client_.Request("PUT", base + "v2/session/" + id,
                    kUserAgent, headers, body, 5, 8, 1024 * 1024,
                    proxy::EnabledUrl(LoadStreamSettings()), abort);
            }
            if (response.status_code < 200 || response.status_code >= 300)
                throw std::runtime_error("CloudMatch refused the session resume request");
            auto root = LoadJson(response.body);
            const auto status = json_object_get(root.get(), "requestStatus");
            if (status && GetInteger(status, "statusCode") != 1)
                throw std::runtime_error("CloudMatch refused the session resume request");
            if (base != LoadActiveCloudSessionBaseUrl(auth, id)) {
                std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
                if (abort()) throw std::runtime_error("Session recovery cancelled");
                auto current = LoadJson(ReadTextFile(GetActiveCloudSessionPath()));
                if (GetString(current.get(), "session_id") != id ||
                    GetString(current.get(), "user_id") != auth.user.user_id)
                    throw std::runtime_error("The active session changed during resume");
                json_object_set_new(current.get(), "streaming_base_url", json_string(base.c_str()));
                WriteJsonToFile(GetActiveCloudSessionPath(), current.get());
            }
            claimed = true;
            AppendSessionTraceLog("RECOVERY RESUME accepted same_session=1; fetching fresh endpoints");
            continue;
        }
        if (state == recovery::CloudState::Ready) {
            const auto previous=info.signaling_url;
            if(!manual_claim) info.signaling_url=gfn::endpoints::RecoveryEndpoint(
                info.signaling_url,info.signaling_candidates,failed_signaling_endpoint);
            AppendSessionTraceLog("ROUTING event=signaling_retry selected=" + gfn::endpoints::SafeRoute(info.signaling_url) +
                " alternate=" + std::to_string(previous!=info.signaling_url) +
                " choices=" + std::to_string(info.signaling_candidates.size()));
            return info; // Use only freshly polled endpoints for this same session.
        }
        for (int i = 0; i < 10 && !abort(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("Session recovery cancelled or timed out");
}

std::optional<ResumableCloudSession> GfnClient::LoadResumableCloudSession(const AuthSession& auth) const
{
    std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
    try {
        auto root = LoadJson(ReadTextFile(GetActiveCloudSessionPath()));
        if (!recovery::Matches(
            {GetString(root.get(), "user_id"), GetString(root.get(), "provider_id"),
             GetString(root.get(), "provider_url"), GetString(root.get(), "device_id"),
             GetString(root.get(), "client_id")},
            {auth.user.user_id, auth.provider.idp_id, auth.provider.streaming_service_url, device_id_, client_id_}))
            return std::nullopt;
        ResumableCloudSession result;
        result.server = GetString(root.get(), "stream_server");
        if (result.server.empty()) result.server = GetString(root.get(), "streaming_base_url");
        const auto scheme = result.server.find("://");
        if (scheme != std::string::npos) result.server.erase(0, scheme + 3);
        result.server = result.server.substr(0, result.server.find_first_of("/?#"));
        result.session_id = GetString(root.get(), "session_id");
        result.launch_app_id = GetString(root.get(), "launch_app_id");
        result.title = GetString(root.get(), "game_title");
        result.store = GetString(root.get(), "launch_store");
        result.started_at_ms = json_integer_value(json_object_get(root.get(), "stream_started_at_ms"));
        result.width = GetInteger(root.get(), "stream_width");
        result.height = GetInteger(root.get(), "stream_height");
        result.fps = GetInteger(root.get(), "stream_fps");
        result.codec = GetString(root.get(), "stream_codec");
        if (result.session_id.empty() || result.launch_app_id.empty() || result.started_at_ms <= 0 ||
            !recovery::ValidFormat(result.width, result.height, result.fps, result.codec))
            return std::nullopt;
        return result;
    } catch (...) { return std::nullopt; }
}

std::chrono::seconds GfnClient::MarkCloudSessionStreaming(const AuthSession& auth,
    const std::string& id, const std::string& title, const std::string& server,const std::string& store) const
{
    std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
    std::chrono::seconds age {0};
    try {
        auto root = LoadJson(ReadTextFile(GetActiveCloudSessionPath()));
        if (GetString(root.get(), "session_id") != id ||
            GetString(root.get(), "user_id") != auth.user.user_id) return std::chrono::seconds(0);
        auto started = json_integer_value(json_object_get(root.get(), "stream_started_at_ms"));
        if (started <= 0) started = NowMs();
        age = std::chrono::seconds(std::max<int64_t>(0, (NowMs() - started) / 1000));
        json_object_set_new(root.get(), "stream_started_at_ms", json_integer(started));
        if(!store.empty())json_object_set_new(root.get(), "launch_store", json_string(store.c_str()));
        json_object_set_new(root.get(), "game_title", json_string(title.c_str()));
        if (!server.empty())
            json_object_set_new(root.get(), "stream_server", json_string(server.c_str()));
        json_object_set_new(root.get(), "device_id", json_string(device_id_.c_str()));
        json_object_set_new(root.get(), "client_id", json_string(client_id_.c_str()));
        if (!json_object_get(root.get(), "stream_width")) {
            const auto settings = LoadStreamSettings();
            json_object_set_new(root.get(), "stream_width", json_integer(settings.width));
            json_object_set_new(root.get(), "stream_height", json_integer(settings.height));
            json_object_set_new(root.get(), "stream_fps", json_integer(settings.fps));
            json_object_set_new(root.get(), "stream_codec", json_string(settings.codec.c_str()));
        }
        WriteJsonToFile(GetActiveCloudSessionPath(), root.get());
        return age;
    } catch (...) {
        AppendSessionTraceLog("RECOVERY unable to persist resume context");
        return age;
    }
}
