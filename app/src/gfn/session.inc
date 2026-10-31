SessionInfo GfnClient::StartSession(AuthSession& session, const std::string& launch_app_id,
                                    const std::string& launch_store,
                                    const std::string& internal_title,
                                    bool force_provider_endpoint) const
{
    network::LaunchPriority launch_priority;
    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string device_id = device_id_;

    const StreamSettings stream_settings = LoadStreamSettings();
    const auto client_profile = gfn::cloud_session::NewClientProfile(session.provider.code);
    if (!force_provider_endpoint)
        routing::Begin(GetAppHome(), server_location::SelectionMode(stream_settings.region),
                       session.provider.streaming_service_url);
    else
        routing::Event(GetAppHome(), "fallback", {{"reason", "caller_requested_provider_endpoint"},
            {"provider_host", routing::Host(session.provider.streaming_service_url)}});
    const std::string trace_heading =
        "StartSession appId=" + launch_app_id +
        " store=" + (launch_store.empty() ? "unknown" : launch_store) +
        " internalTitle=" +
        (internal_title.empty() ? "missing" : internal_title);
    if (force_provider_endpoint)
        AppendSessionTraceLog("START provider fallback begin " + trace_heading);
    else
        ResetSessionTraceLog(trace_heading);

    std::string automatic_region_url;
    std::string automatic_region_name;
    if (server_location::IsAutomatic(stream_settings.region) &&
        !force_provider_endpoint)
    {
        const std::string provider_url =
            server_location::NormalizeStreamingBaseUrl(
                session.provider.streaming_service_url);
        {
            std::lock_guard<std::mutex> lock(g_region_selection_cache.mutex);
            if (g_region_selection_cache.provider_url == provider_url &&
                !g_region_selection_cache.provider_fallback) {
                FilterZoneCatalog(g_region_selection_cache.regions, ZoneCatalogEligible(session));
                ApplyRouteHistory(g_region_selection_cache.regions, provider_url);
                const auto best = server_zone::Select(g_region_selection_cache.regions, stream_settings.region, NowMs());
                g_region_selection_cache.selected_url = best;
                if (!best.empty()) {
                    g_region_selection_cache.selected_url = best;
                    for (const auto& candidate : g_region_selection_cache.regions)
                        if (server_location::NormalizeStreamingBaseUrl(candidate.url) == best)
                            g_region_selection_cache.selected_name = candidate.name;
                }
            }
            const auto age =
                std::chrono::steady_clock::now() -
                g_region_selection_cache.measured_at;
            const auto cache_lifetime =
                g_region_selection_cache.provider_fallback
                    ? kProviderFallbackCacheLifetime
                    : kRegionSelectionCacheLifetime;
            if (g_region_selection_cache.provider_url == provider_url &&
                age < cache_lifetime &&
                (g_region_selection_cache.provider_fallback ||
                 !server_location::CachedRouteNeedsProbe(g_region_selection_cache.regions,
                     g_region_selection_cache.selected_url, NowMs())))
            {
                automatic_region_url =
                    g_region_selection_cache.selected_url;
                automatic_region_name =
                    g_region_selection_cache.selected_name;
                routing::Event(GetAppHome(), "cache_hit", {{"source", "memory"},
                    {"age_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(age).count())},
                    {"selected_host", routing::Host(automatic_region_url)},
                    {"provider_fallback", g_region_selection_cache.provider_fallback ? "1" : "0"}});
                routing::Candidates(GetAppHome(), g_region_selection_cache.regions, "memory_cache", automatic_region_url);
                AppendSessionTraceLog(
                    "REGION automatic memory cache hit ageMs=" +
                    std::to_string(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            age).count()) +
                    " selected=" + automatic_region_url +
                    " name=" + automatic_region_name +
                    " fallback=" +
                    std::to_string(
                        g_region_selection_cache.provider_fallback ? 1 : 0));
            }
        }

        if (automatic_region_url.empty())
        {
            std::lock_guard<std::mutex> lock(g_region_selection_cache.mutex);
            const PersistedRegionSelection persisted =
                LoadPersistentRegionSelection(
                    provider_url, kPersistentRegionCacheLifetimeMs, ZoneCatalogEligible(session));
            if (persisted.valid && !server_location::CachedRouteNeedsProbe(
                    persisted.regions, persisted.selected_url, NowMs()))
            {
                automatic_region_url = persisted.selected_url;
                automatic_region_name = persisted.selected_name;
                g_region_selection_cache.provider_url = provider_url;
                g_region_selection_cache.selected_url = persisted.selected_url;
                g_region_selection_cache.selected_name = persisted.selected_name;
                g_region_selection_cache.regions = persisted.regions;
                g_region_selection_cache.measured_at =
                    std::chrono::steady_clock::now() -
                        std::chrono::milliseconds(NowMs() - persisted.measured_at_ms);
                g_region_selection_cache.provider_fallback = false;
                routing::Event(GetAppHome(), "cache_hit", {{"source", "persistent"},
                    {"age_ms", std::to_string(NowMs() - persisted.measured_at_ms)},
                    {"selected_host", routing::Host(automatic_region_url)}});
                routing::Candidates(GetAppHome(), persisted.regions, "persistent_cache", automatic_region_url);
                AppendSessionTraceLog(
                    "REGION automatic persistent cache hit selected=" +
                    automatic_region_url + " name=" + automatic_region_name);
            }
            else {
                routing::Event(GetAppHome(), "remeasure", {{"reason", persisted.valid
                    ? "cached_route_has_playback_pauses" : "cache_missing_expired_or_invalid"}});
                AppendSessionTraceLog(std::string("REGION automatic remeasure reason=") +
                    (persisted.valid ? "observed_playback_pauses" : "missing_or_expired_probe_cache"));
            }
        }

        if (automatic_region_url.empty())
        {
            try
            {
                const std::vector<StreamRegion> measured =
                    MeasureStreamRegionLatencies(FetchStreamRegions(session), session.provider.streaming_service_url);
                for (const StreamRegion& region : measured)
                {
                    AppendSessionTraceLog(
                        "REGION candidate name=" + region.name +
                        " tcpEstimateMs=" + std::to_string(region.ping_ms) +
                        " observedStreamRttMs=" +
                        std::to_string(region.observed_stream_rtt_ms) +
                        " jitterMs=" + std::to_string(region.jitter_ms) +
                        " scoreMs=" +
                        std::to_string(server_location::RegionSelectionScore(region)) +
                        " tcpScoreMs=" + std::to_string(region.selection_score_ms) +
                        " qualityPenaltyMs=" + std::to_string(server_location::ObservedQualityPenalty(region, NowMs())) +
                        " playbackPauses/countMs=" + std::to_string(region.observed_playback_pause_count) + "/" +
                        std::to_string(region.observed_playback_pause_ms) +
                        " probes=" +
                        std::to_string(region.successful_probes) + "/" +
                        std::to_string(region.attempted_probes) +
                        " url=" + region.url);
                }
                automatic_region_url =
                    server_zone::Select(measured, stream_settings.region, NowMs());
                routing::Candidates(GetAppHome(), measured, "fresh_measurements", automatic_region_url);
                routing::Event(GetAppHome(), "selection", {{"selected_host", routing::Host(automatic_region_url)},
                    {"reason", automatic_region_url.empty() ? "no_successful_candidate_provider_default" : "lowest_combined_score"}});
                const auto selected = std::find_if(
                    measured.begin(), measured.end(),
                    [&automatic_region_url](const StreamRegion& region) {
                        return server_location::NormalizeStreamingBaseUrl(
                                   region.url) == automatic_region_url;
                    });
                if (selected != measured.end())
                    automatic_region_name = selected->name;
                AppendSessionTraceLog(
                    "REGION automatic selected=" +
                    (automatic_region_url.empty()
                         ? std::string("provider-default")
                         : automatic_region_url) +
                    " name=" + automatic_region_name);

                if (!automatic_region_url.empty())
                    SaveStreamRegionMeasurements(session, measured);
            }
            catch (const std::exception& e)
            {
                routing::Event(GetAppHome(), "fallback", {{"reason", "measurement_failed"},
                    {"provider_host", routing::Host(session.provider.streaming_service_url)}});
                AppendSessionTraceLog(
                    "REGION automatic measurement failed; using provider endpoint error=" +
                    std::string(e.what()));
            }
        }
    }

    const std::string streaming_base_url =
        server_location::ResolveStreamingBaseUrl(
            stream_settings.region,
            session.provider.streaming_service_url,
            automatic_region_url);
    if (streaming_base_url.empty())
        throw std::runtime_error("No valid GeForce NOW streaming endpoint is available.");
    routing::Event(GetAppHome(), "request", {{"requested_host", routing::Host(streaming_base_url)},
        {"mode", server_location::SelectionMode(stream_settings.region)},
        {"manual_region", routing::Host(stream_settings.region)},
        {"force_provider", force_provider_endpoint ? "1" : "0"}});

    std::string url = streaming_base_url +
        "v2/session?keyboardLayout=en-US_qwerty&languageCode=" +
        stream_settings.game_language;

    std::vector<std::string> headers = gfn::cloud_session::BuildSessionHeaders(
        jwt_token, client_id_, device_id, client_profile);

    const auto sub_session_id = gfn::cloud_session::SubSessionIdFromRandom(GenerateRandomBytes(16));
    std::string body = gfn::cloud_session::BuildSessionBody(
        launch_app_id, internal_title, device_id, stream_settings, sub_session_id, client_profile);
    AppendSessionTraceLog("ROUTING event=launch_context subSessionFresh=1 deviceIdentityStable=1 profile=" +
        std::string(gfn::cloud_session::ProfileName(client_profile)));
    const std::string proxy_url = proxy::EnabledUrl(stream_settings);
    CleanupStaleCloudSession(session);
    jwt_token = ResolveSessionJwt(session);
    headers[0] = "Authorization: GFNJWT " + jwt_token;
    AppendSessionTraceLog("START url=" + url);
    AppendSessionTraceLog(
        "START region=" + stream_settings.region +
        " resolvedBase=" + streaming_base_url +
        (force_provider_endpoint ? " fallback=provider-endpoint" : "") +
        (automatic_region_url.empty()
             ? ""
             : " measuredBest=" + automatic_region_url));
    AppendSessionTraceLog(
        "START settings=" + std::to_string(stream_settings.width) + "x" +
        std::to_string(stream_settings.height) + "@" + std::to_string(stream_settings.fps) +
        " bitrate=" + std::to_string(stream_settings.bitrate_kbps) +
        "kbps language=" + stream_settings.game_language +
        " persistGameSettings=" +
        std::to_string(stream_settings.persist_game_settings ? 1 : 0) +
        " deviceId=" + device_id);
    AppendSessionTraceLog("START headers:\n" + HeadersForTrace(headers));
    AppendSessionTraceLog("START request body:\n" + JsonForTrace(body));

    HttpResponse response = http_client_.Request(
        "POST", url, kUserAgent, headers, body, 10, 30, 0, proxy_url);
    if (response.status_code == 401)
    {
        session = ForceRefreshSavedSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        AppendSessionTraceLog("START authorization rejected; refreshed token and retrying once");
        response = http_client_.Request(
            "POST", url, kUserAgent, headers, body, 10, 30, 0, proxy_url);
    }
    AppendSessionTraceLog("START response HTTP " + std::to_string(response.status_code));
    AppendSessionTraceLog("START response body:\n" + JsonForTrace(response.body));

    auto parse_start_response = [&response]() -> JsonPtr {
        try {
            return LoadJson(response.body);
        } catch (const std::exception& e) {
            AppendSessionTraceLog(std::string("START JSON parse error: ") + e.what());
            return JsonPtr(nullptr, &json_decref);
        }
    };
    JsonPtr root = parse_start_response();

    if (root && IsDeviceSessionLimitResponse(root.get()))
    {
        AppendSessionTraceLog(
            "START device session limit detected; refreshing the server session list, "
            "cleaning matching sessions and retrying once");
        CleanupStaleCloudSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        response = http_client_.Request(
            "POST", url, kUserAgent, headers, body, 10, 30, 0, proxy_url);
        AppendSessionTraceLog("START retry response HTTP " + std::to_string(response.status_code));
        AppendSessionTraceLog("START retry response body:\n" + JsonForTrace(response.body));
        root = parse_start_response();
    }

    const bool app_patching = root && IsAppPatchingResponse(root.get());
    if (root)
    {
        json_t* req_status = json_object_get(root.get(), "requestStatus");
        if (req_status && GetInteger(req_status, "statusCode") != 1 && !app_patching)
        {
            const std::string error = BuildCloudMatchErrorMessage("StartSession", response.status_code, root.get());
            AppendSessionTraceLog("START api error: " + error);
            throw std::runtime_error(error);
        }
    }

    if (response.status_code != 200 && response.status_code != 202 && !app_patching)
    {
        const std::string error = BuildCloudMatchErrorMessage("StartSession", response.status_code, root.get());
        AppendSessionTraceLog("START http error: " + error);
        throw std::runtime_error(error);
    }

    if (!root) {
        AppendSessionTraceLog("START failed: empty or invalid JSON");
        throw std::runtime_error("StartSession failed: Empty or invalid JSON" + SessionTraceHint());
    }

    SessionInfo info;
    info.app_patching = app_patching;
    info.cloud_client_profile = client_profile;
    info.requested_region_url = streaming_base_url;
    json_t* request_status = json_object_get(root.get(), "requestStatus");
    info.assigned_server_id = GetString(request_status, "serverId");
    CollectIceServersFromObject(root.get(), info);
    
    json_t* sess = json_object_get(root.get(), "session");
    if (sess)
    {
        json_t* s_id = json_object_get(sess, "sessionId");
        if (s_id && json_is_string(s_id))
            info.session_id = json_string_value(s_id);
            
        json_t* status = json_object_get(sess, "status");
        if (status)
            info.status = gfn::cloud_session::ParseSessionStatus(status);
            
        ApplySessionAds(sess, info);

        ApplySessionNetworkInfo(sess, info);
    }
    
    gfn::cloud_session::ApplyQueueState(root.get(), info);

    if (info.session_id.empty()) {
        AppendSessionTraceLog("START failed: session ID missing after successful response");
        throw std::runtime_error("Failed to parse session ID from response" + SessionTraceHint());
    }

    if (app_patching)
        AppendSessionTraceLog("START pending reason=app_patching; polling created session");
    RememberActiveCloudSession(
        session,
        info.session_id,
        launch_app_id,
        streaming_base_url,
        device_id_, client_id_, client_profile);
    TrackSessionAssignment(info);
    AppendSessionTraceLog("START parsed: " + SessionInfoTraceSummary(info));
    routing::Event(GetAppHome(), "created", {{"requested_host", routing::Host(streaming_base_url)},
        {"cloud_session", info.session_id},
        {"assigned_node", routing::Host(info.assigned_server_id)}, {"server_host", routing::Host(info.server_ip)},
        {"status", std::to_string(info.status)}, {"queue", std::to_string(info.queue_position)}});
    return info;
}

SessionInfo GfnClient::PollSession(AuthSession& session, const std::string& session_id,
    const std::function<bool()>& is_cancelled, long timeout_seconds) const
{
    if (is_cancelled && is_cancelled()) throw std::runtime_error("Session recovery cancelled");
    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string device_id = device_id_;
    const auto client_profile = LoadActiveCloudSessionProfile(session,session_id);
    const std::string active_base_url =
        LoadActiveCloudSessionBaseUrl(session, session_id);
    std::string base_url = active_base_url.empty()
        ? server_location::ResolveStreamingBaseUrl(
              "Auto",
              session.provider.streaming_service_url)
        : active_base_url;
    std::string url = base_url + "v2/session/" + session_id;
    const std::string proxy_url =
        proxy::EnabledUrl(LoadStreamSettings());

    std::vector<std::string> headers = gfn::cloud_session::BuildSessionHeaders(
        jwt_token, client_id_, device_id, client_profile);

    AppendSessionTraceLog("POLL url=" + url);
    AppendSessionTraceLog("POLL headers:\n" + HeadersForTrace(headers));
    // A verified direct route can later become unreachable. Retain one bounded
    // same-session regional fallback, without creating or claiming another rig.
    std::string fallback_base;
    if (session.provider.code == "NVIDIA" && server_zone::IsDirectControlHost(server_location::RouteHost(base_url))) {
        std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
        auto saved = LoadJson(ReadTextFile(GetActiveCloudSessionPath()));
        if (GetString(saved.get(), "session_id") == session_id && recovery::Matches(
            {GetString(saved.get(), "user_id"), GetString(saved.get(), "provider_id"),
             GetString(saved.get(), "provider_url"), GetString(saved.get(), "device_id"), GetString(saved.get(), "client_id")},
            {session.user.user_id, session.provider.idp_id, session.provider.streaming_service_url, device_id_, client_id_})) {
            auto candidate = server_location::NormalizeStreamingBaseUrl(GetString(saved.get(), "streaming_fallback_base_url"));
            if (server_zone::IsCloudMatchHost(candidate) && candidate != base_url) fallback_base = candidate;
        }
    }
    const auto poll_request = [&](long timeout) {
        return http_client_.Request("GET", url, kUserAgent, headers, {}, std::min(10L, timeout), timeout,
            1024 * 1024, proxy_url, is_cancelled);
    };
    HttpResponse response;
    try {
        response = poll_request(fallback_base.empty() ? timeout_seconds : std::min(3L, timeout_seconds));
    } catch (...) {
        if (fallback_base.empty() || (is_cancelled && is_cancelled())) throw;
        routing::Event(GetAppHome(), "direct_poll", {{"result", "transport_failed_try_regional_same_session"}});
    }
    if (!fallback_base.empty() && (response.status_code == 0 || response.status_code == 404 || response.status_code >= 500)) {
        base_url = fallback_base;
        url = base_url + "v2/session/" + session_id;
        response = poll_request(timeout_seconds);
        routing::Event(GetAppHome(), "direct_poll", {{"result", "regional_fallback"}, {"http", std::to_string(response.status_code)}});
    }
    if (response.status_code == 401)
    {
        session = ForceRefreshSavedSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        AppendSessionTraceLog("POLL authorization rejected; refreshed token and retrying once");
        response = http_client_.Request(
            "GET", url, kUserAgent, headers, {}, std::min(10L, timeout_seconds), timeout_seconds,
            1024 * 1024, proxy_url, is_cancelled);
    }
    AppendSessionTraceLog("POLL response HTTP " + std::to_string(response.status_code));
    if (response.status_code == 404)
        throw CloudSessionNotFound("The cloud session is no longer available");
    AppendSessionTraceLog("POLL response body:\n" + JsonForTrace(response.body));

    JsonPtr root(nullptr, &json_decref);
    try {
        root = LoadJson(response.body);
    } catch (const std::exception& e) {
        AppendSessionTraceLog(std::string("POLL JSON parse error: ") + e.what());
    }

    const bool app_patching = root && IsAppPatchingResponse(root.get());
    json_t* request_status =
        root ? json_object_get(root.get(), "requestStatus") : nullptr;
    const int request_status_code =
        request_status ? GetInteger(request_status, "statusCode") : 0;
    const std::string request_status_description =
        request_status ? GetString(request_status, "statusDescription") : "";
    const std::string provider_base_url =
        server_location::NormalizeStreamingBaseUrl(
            session.provider.streaming_service_url);
    const bool automatic_direct_endpoint =
        server_location::IsAutomatic(LoadStreamSettings().region) &&
        !active_base_url.empty() &&
        !provider_base_url.empty() &&
        server_location::NormalizeStreamingBaseUrl(active_base_url) !=
            provider_base_url;
    const bool invalid_session_id =
        request_status_code == 22 &&
        request_status_description.find(
            "INVALID_SESSION_ID_NOT_FOUND") != std::string::npos;
    if (invalid_session_id && automatic_direct_endpoint)
    {
        const std::string error =
            BuildCloudMatchErrorMessage(
                "PollSession", response.status_code, root.get());
        {
            std::lock_guard<std::mutex> lock(
                g_region_selection_cache.mutex);
            g_region_selection_cache.provider_url = provider_base_url;
            g_region_selection_cache.selected_url = provider_base_url;
            g_region_selection_cache.selected_name = "NVIDIA automatic routing";
            g_region_selection_cache.measured_at =
                std::chrono::steady_clock::now();
            g_region_selection_cache.provider_fallback = true;
        }
        AppendSessionTraceLog(
            "POLL automatic region lost session; provider fallback requested "
            "and cached for this provider: " +
            error);
        throw AutomaticRegionSessionLost(error);
    }
    if (invalid_session_id)
    {
        const std::string error =
            BuildCloudMatchErrorMessage(
                "PollSession", response.status_code, root.get());
        AppendSessionTraceLog(
            "POLL session no longer exists at provider endpoint: " +
            error);
        throw CloudSessionNotFound(error);
    }
    if (root)
    {
        json_t* req_status = json_object_get(root.get(), "requestStatus");
        if (req_status && GetInteger(req_status, "statusCode") != 1 && !app_patching)
        {
            const std::string error = BuildCloudMatchErrorMessage("PollSession", response.status_code, root.get());
            AppendSessionTraceLog("POLL api error: " + error);
            throw std::runtime_error(error);
        }
    }

    if (response.status_code != 200 && !app_patching)
    {
        const std::string error = BuildCloudMatchErrorMessage("PollSession", response.status_code, root.get());
        AppendSessionTraceLog("POLL http error: " + error);
        throw std::runtime_error(error);
    }

    if (!root) {
        AppendSessionTraceLog("POLL failed: empty or invalid JSON");
        throw std::runtime_error("PollSession failed: Empty or invalid JSON" + SessionTraceHint());
    }

    // Keep diagnostics useful without persisting session tokens returned by CloudMatch.
    const std::string redacted_session = JsonForTrace(response.body);
    FILE* session_dump = fopen("sdmc:/switch/SwitchNOW/switchnow_session.json", "w");
    if (session_dump) {
        fputs(redacted_session.c_str(), session_dump);
        fclose(session_dump);
    }

    SessionInfo info;
    info.cloud_client_profile = client_profile;
    info.session_id = session_id;
    info.app_patching = app_patching;
    info.requested_region_url = base_url;
    json_t* response_request_status =
        json_object_get(root.get(), "requestStatus");
    info.assigned_server_id =
        GetString(response_request_status, "serverId");
    CollectIceServersFromObject(root.get(), info);
    
    json_t* sess = json_object_get(root.get(), "session");
    if (sess)
    {
        const std::string returned_id = GetString(sess, "sessionId");
        if (!returned_id.empty() && returned_id != session_id)
            throw std::runtime_error("CloudMatch returned a different session; refusing reconnect");
        json_t* status = json_object_get(sess, "status");
        if (status)
            info.status = gfn::cloud_session::ParseSessionStatus(status);
            
        ApplySessionAds(sess, info);
            
        ApplySessionNetworkInfo(sess, info);
    }
    
    gfn::cloud_session::ApplyQueueState(root.get(), info);

    if (session.provider.code == "NVIDIA" && server_zone::ShouldDirectPoll(session.provider.streaming_service_url, base_url, info)) {
        const auto control_host = server_zone::ControlHost(info);
        static std::mutex direct_poll_mutex;
        static std::deque<std::pair<std::pair<std::string, std::string>, unsigned>> attempts;
        bool allowed = false;
        {
            std::lock_guard<std::mutex> lock(direct_poll_mutex);
            const auto key = std::make_pair(session_id, control_host);
            auto it = std::find_if(attempts.begin(), attempts.end(), [&](const auto& entry) { return entry.first == key; });
            if (it == attempts.end()) {
                if (attempts.size() >= 16) attempts.pop_front();
                attempts.push_back({key, 0});
                it = std::prev(attempts.end());
            }
            allowed = it->second < 3;
            if (allowed) ++it->second;
        }
        if (allowed) {
            routing::Event(GetAppHome(), "direct_poll", {{"cloud_session", session_id},
                {"control_host", routing::Host(control_host)}, {"result", "begin"}});
            try {
                const auto direct = http_client_.Request("GET", "https://" + control_host + "/v2/session/" + session_id,
                    kUserAgent, headers, {}, std::min(3L, timeout_seconds), std::min(3L, timeout_seconds),
                    1024*1024, {}, is_cancelled);
                auto payload = LoadJson(direct.body);
                auto* returned = json_object_get(payload.get(), "session");
                SessionInfo candidate;
                candidate.cloud_client_profile = client_profile;
                candidate.session_id = GetString(returned, "sessionId");
                candidate.status = gfn::cloud_session::ParseSessionStatus(json_object_get(returned, "status"));
                candidate.requested_region_url = info.requested_region_url;
                candidate.assigned_server_id = GetString(json_object_get(payload.get(), "requestStatus"), "serverId");
                ApplySessionNetworkInfo(returned, candidate);
                if (candidate.server_ip.empty()) candidate.server_ip = info.server_ip;
                if (candidate.assigned_server_id.empty()) candidate.assigned_server_id = info.assigned_server_id;
                CollectIceServersFromObject(payload.get(), candidate);
                ApplySessionAds(returned, candidate);
                gfn::cloud_session::ApplyQueueState(payload.get(), candidate);
                const bool accepted = direct.status_code == 200 &&
                    GetInteger(json_object_get(payload.get(), "requestStatus"), "statusCode") == 1 &&
                    server_zone::AcceptDirectPoll(info, candidate);
                bool pinned = false;
                if (accepted) {
                    // Pin the verified control route so later recovery does not revert to the LB.
                    std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
                    auto saved = LoadJson(ReadTextFile(GetActiveCloudSessionPath()));
                    if (GetString(saved.get(), "session_id") == session_id && recovery::Matches(
                        {GetString(saved.get(), "user_id"), GetString(saved.get(), "provider_id"),
                         GetString(saved.get(), "provider_url"), GetString(saved.get(), "device_id"), GetString(saved.get(), "client_id")},
                        {session.user.user_id, session.provider.idp_id, session.provider.streaming_service_url, device_id_, client_id_})) {
                        if (is_cancelled && is_cancelled()) throw std::runtime_error("Session recovery cancelled");
                        if (GetString(saved.get(), "streaming_fallback_base_url").empty())
                            json_object_set_new(saved.get(), "streaming_fallback_base_url", json_string(base_url.c_str()));
                        json_object_set_new(saved.get(), "streaming_base_url",
                            json_string(("https://" + control_host + "/").c_str()));
                        WriteJsonToFile(GetActiveCloudSessionPath(), saved.get());
                        info = std::move(candidate);
                        pinned = true;
                    }
                }
                routing::Event(GetAppHome(), "direct_poll", {{"cloud_session", session_id},
                    {"control_host", routing::Host(control_host)}, {"http", std::to_string(direct.status_code)},
                    {"result", pinned ? "accepted_same_session" : accepted ? "identity_changed_original_kept" : "original_response_kept"}});
            } catch (...) {
                if (is_cancelled && is_cancelled()) throw;
                routing::Event(GetAppHome(), "direct_poll", {{"result", "failed_original_response_kept"}});
            }
        } else {
            routing::Event(GetAppHome(), "direct_poll", {{"result", "retry_budget_exhausted_original_kept"}});
        }
    } else if (info.status == 2 || info.status == 3) {
        routing::Event(GetAppHome(), "direct_poll", {{"result", "skipped"},
            {"provider_official", session.provider.code == "NVIDIA" ? "1" : "0"},
            {"control_host", routing::Host(server_zone::ControlHost(info))},
            {"control_eligible", server_zone::IsDirectControlHost(server_zone::ControlHost(info)) ? "1" : "0"},
            {"already_direct", server_location::RouteHost(base_url) == server_zone::ControlHost(info) ? "1" : "0"}});
    }

    if (app_patching)
        AppendSessionTraceLog("POLL pending reason=app_patching");
    TrackSessionAssignment(info);
    AppendSessionTraceLog("POLL parsed: " + SessionInfoTraceSummary(info));
    if ((info.status == 2 || info.status == 3) && !info.media_ip.empty())
        routing::Event(GetAppHome(), "ready", {{"assigned_node", routing::Host(info.assigned_server_id)},
            {"initial_host", routing::Host(info.initial_server_ip)}, {"redirects", std::to_string(info.server_redirects)},
            {"requested_host", routing::Host(info.requested_region_url)}, {"server_host", routing::Host(info.server_ip)}, {"control_host", routing::Host(info.control_ip)},
            {"media_host", routing::Host(info.media_ip)}, {"media_port", std::to_string(info.media_port)}});
    return info;
}

SessionInfo GfnClient::ReportSessionAd(
    AuthSession& session,
    const std::string& session_id,
    const SessionAdReport& report,
    const std::string& server_ip) const
{
    if (session_id.empty() || report.ad_id.empty())
        throw std::runtime_error("Cannot report an advertisement without a session and ad ID");

    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string active_base_url =
        LoadActiveCloudSessionBaseUrl(session, session_id);
    std::string base_url = active_base_url.empty()
        ? server_location::ResolveStreamingBaseUrl(
              "Auto", session.provider.streaming_service_url)
        : active_base_url;
    std::string endpoint_source = active_base_url.empty()
        ? "provider" : "tracked_region";

    // NVIDIA's queue-ad state is owned by the CloudMatch node assigned to the
    // session. Sending the update through the regional load balancer can hit a
    // different backend and intermittently return INVALID_REQUEST_STATUS on
    // the first launch. Alliance providers keep their own routing contract.
    const bool official_nvidia =
        session.provider.code == "NVIDIA" ||
        session.provider.streaming_service_url.find("nvidiagrid.net") !=
            std::string::npos;
    const bool has_scheme =
        HasPrefix(server_ip, "https://") || HasPrefix(server_ip, "http://");
    const bool usable_server =
        !server_ip.empty() &&
        server_ip.find_first_of(" \t\r\n?#") == std::string::npos;
    if (official_nvidia && usable_server)
    {
        base_url = EnsureTrailingSlash(
            has_scheme ? server_ip : "https://" + server_ip);
        endpoint_source = "assigned_server";
    }
    const std::string url = base_url + "v2/session/" + session_id;

    std::vector<std::string> headers = gfn::cloud_session::BuildSessionHeaders(
        jwt_token, client_id_, device_id_, LoadActiveCloudSessionProfile(session,session_id));

    JsonPtr update(json_object(), &json_decref);
    json_object_set_new(update.get(), "adId", json_string(report.ad_id.c_str()));
    json_object_set_new(
        update.get(), "adAction",
        json_integer(static_cast<int>(report.action)));
    json_object_set_new(
        update.get(), "clientTimestamp",
        json_integer(static_cast<json_int_t>(std::time(nullptr))));
    if (report.watched_time_ms >= 0)
        json_object_set_new(
            update.get(), "watchedTimeInMs",
            json_integer(report.watched_time_ms));
    if (report.paused_time_ms >= 0)
        json_object_set_new(
            update.get(), "pausedTimeInMs",
            json_integer(report.paused_time_ms));
    if (!report.cancel_reason.empty())
        json_object_set_new(
            update.get(), "cancelReason",
            json_string(report.cancel_reason.c_str()));

    JsonPtr updates(json_array(), &json_decref);
    json_array_append_new(updates.get(), json_incref(update.get()));
    JsonPtr request(json_object(), &json_decref);
    json_object_set_new(request.get(), "action", json_integer(6));
    json_object_set_new(request.get(), "adUpdates", json_incref(updates.get()));
    const std::string body = DumpJson(request.get());
    const std::string proxy_url = endpoint_source == "assigned_server"
        ? std::string {}
        : proxy::EnabledUrl(LoadStreamSettings());

    AppendSessionTraceLog(
        "AD update action=" +
        std::to_string(static_cast<int>(report.action)) +
        " watchedMs=" + std::to_string(report.watched_time_ms) +
        " pausedMs=" + std::to_string(report.paused_time_ms) +
        " endpoint=" + endpoint_source +
        " host=" + base_url);
    HttpResponse response = http_client_.Request(
        "PUT", url, kUserAgent, headers, body, 10, 30, 0, proxy_url);
    if (response.status_code == 401)
    {
        session = ForceRefreshSavedSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        response = http_client_.Request(
            "PUT", url, kUserAgent, headers, body, 10, 30, 0, proxy_url);
    }

    JsonPtr root(nullptr, &json_decref);
    try
    {
        root = LoadJson(response.body);
    }
    catch (const std::exception& e)
    {
        AppendSessionTraceLog(
            "AD update JSON parse error=" + std::string(e.what()));
    }

    json_t* request_status =
        root ? json_object_get(root.get(), "requestStatus") : nullptr;
    if (response.status_code < 200 || response.status_code >= 300 ||
        !request_status || GetInteger(request_status, "statusCode") != 1)
    {
        const std::string error = root
            ? BuildCloudMatchErrorMessage(
                  "ReportSessionAd", response.status_code, root.get())
            : "ReportSessionAd failed with HTTP " +
                  std::to_string(response.status_code) + SessionTraceHint();
        AppendSessionTraceLog("AD update failed: " + error);
        throw std::runtime_error(error);
    }

    SessionInfo info;
    info.session_id = session_id;
    CollectIceServersFromObject(root.get(), info);
    json_t* sess = json_object_get(root.get(), "session");
    if (sess)
    {
        json_t* status = json_object_get(sess, "status");
        if (status)
            info.status = gfn::cloud_session::ParseSessionStatus(status);
        ApplySessionAds(sess, info);
        ApplySessionNetworkInfo(sess, info);
    }
    gfn::cloud_session::ApplyQueueState(root.get(), info);
    TrackSessionAssignment(info);
    AppendSessionTraceLog("AD update accepted: " + SessionInfoTraceSummary(info));
    return info;
}

void GfnClient::StopSession(
    AuthSession& session,
    const std::string& session_id,
    const std::string& server_ip) const
{
    network::LaunchPriority launch_priority;
    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string device_id = device_id_;
    if(session_id.empty()) throw std::runtime_error("Cannot close a cloud session without its ID");
    const auto saved_base=LoadActiveCloudSessionBaseUrl(session,session_id);
    const auto provider_base=server_location::ResolveStreamingBaseUrl("Auto",session.provider.streaming_service_url);
    const auto routes=gfn::cleanup::Routes(server_ip,saved_base,provider_base);
    const auto profile=LoadActiveCloudSessionProfile(session,session_id);
    auto headers=gfn::cloud_session::BuildSessionHeaders(jwt_token,client_id_,device_id,profile);
    const auto proxy=proxy::EnabledUrl(LoadStreamSettings());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    HttpCancellationScope stop_scope([deadline] {return std::chrono::steady_clock::now()>=deadline;});
    const auto cancelled=HttpCancellationScope::Callback();
    AppendSessionTraceLog("STOP routes="+std::to_string(routes.size())+
        " invalid_hint="+std::to_string(!server_ip.empty() && gfn::endpoints::ControlBaseUrl(server_ip).empty())+
        " profile="+gfn::cloud_session::ProfileName(profile));
    const auto result=gfn::cleanup::Close(routes,
        [&](const gfn::cleanup::Route& route) {
            const auto url=route.base+"v2/session/"+UrlEncode(session_id);
            auto response=http_client_.Request("DELETE",url,kUserAgent,headers,{},3,6,1024*1024,
                (route.source=="server_hint" || (server_zone::IsOfficialProvider(session.provider.streaming_service_url) &&
                    server_zone::IsDirectControlHost(server_location::RouteHost(route.base)))) ? std::string{} : proxy,cancelled);
            return gfn::cleanup::Reply{response.status_code,std::move(response.body)};
        },
        [&] {
            session=ForceRefreshSavedSession(session);
            headers[0]="Authorization: GFNJWT "+ResolveSessionJwt(session);
        },
        [&](const gfn::cleanup::Route& route,const char* event,int status) {
            routing::Event(GetAppHome(),"cleanup_route",{{"source",route.source},
                {"route",gfn::endpoints::SafeRoute(route.base)}, {"result",event},{"http",std::to_string(status)}});
        },cancelled);
    AppendSessionTraceLog(std::string("STOP completed result=")+
        (result==gfn::cleanup::Result::Accepted ? "accepted" : "already_absent"));
    ForgetActiveCloudSession(session_id);

}

void GfnClient::CleanupStaleCloudSession(AuthSession& session) const
{
    const std::string stale_session_id = LoadActiveCloudSessionId(session);
    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string list_url = session.provider.streaming_service_url + "v2/session";
    const std::string proxy_url =
        proxy::EnabledUrl(LoadStreamSettings());
    std::vector<std::string> headers = gfn::cloud_session::BuildSessionHeaders(
        jwt_token, client_id_, device_id_, stale_session_id.empty()
            ? gfn::cloud_session::NewClientProfile(session.provider.code)
            : LoadActiveCloudSessionProfile(session,stale_session_id));

    AppendSessionTraceLog("CLEANUP list url=" + list_url);
    HttpResponse response = http_client_.Request(
        "GET", list_url, kUserAgent, headers, {}, 10, 30, 0, proxy_url);
    if (response.status_code == 401)
    {
        session = ForceRefreshSavedSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        AppendSessionTraceLog(
            "CLEANUP authorization rejected; refreshed token and retrying session list once");
        response = http_client_.Request(
            "GET", list_url, kUserAgent, headers, {}, 10, 30, 0, proxy_url);
    }
    AppendSessionTraceLog("CLEANUP list response HTTP " + std::to_string(response.status_code));
    AppendSessionTraceLog("CLEANUP list response body:\n" + JsonForTrace(response.body));

    bool usable_list = false;
    bool tracked_session_seen = false;
    std::string cleanup_error;
    if (response.status_code >= 200 && response.status_code < 300)
    {
        try
        {
            JsonPtr root = LoadJson(response.body);
            json_t* request_status = json_object_get(root.get(), "requestStatus");
            json_t* sessions = json_object_get(root.get(), "sessions");
            usable_list =
                request_status &&
                GetInteger(request_status, "statusCode") == 1 &&
                json_is_array(sessions);

            if (usable_list)
            {
                size_t index = 0;
                json_t* cloud_session = nullptr;
                json_array_foreach(sessions, index, cloud_session)
                {
                    const std::string session_id = GetString(cloud_session, "sessionId");
                    const int status = gfn::cloud_session::ParseSessionStatus(
                        json_object_get(cloud_session, "status"));
                    const std::string session_device_id = GetString(
                        json_object_get(cloud_session, "sessionRequestData"),
                        "deviceHashId");
                    if (session_id == stale_session_id)
                        tracked_session_seen = true;

                    if (!cloud_session::ShouldCleanup(
                            status,
                            session_id,
                            session_device_id,
                            device_id_,
                            stale_session_id))
                    {
                        continue;
                    }

                    const std::string server_ip = GetSessionServerIp(cloud_session);
                    AppendSessionTraceLog(
                        "CLEANUP stopping session id=" + session_id +
                        " status=" + std::to_string(status) +
                        " source=" +
                        (session_id == stale_session_id ? "local_record" : "matching_device") +
                        " server=" + (server_ip.empty() ? "provider" : server_ip));
                    try
                    {
                        StopSession(session, session_id, server_ip);
                    }
                    catch (const std::exception& e)
                    {
                        cleanup_error = e.what();
                        AppendSessionTraceLog(
                            "CLEANUP stop failed id=" + session_id +
                            " error=" + cleanup_error);
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            AppendSessionTraceLog(
                "CLEANUP list parse failed error=" + std::string(e.what()));
        }
    }

    if (usable_list)
    {
        if (!stale_session_id.empty() && !tracked_session_seen)
            ForgetActiveCloudSession(stale_session_id);
        if (!cleanup_error.empty())
            throw std::runtime_error(cleanup_error);
        return;
    }

    // Older or temporarily unavailable list endpoints still get the original
    // local-record cleanup path.
    if (!stale_session_id.empty())
    {
        AppendSessionTraceLog(
            "CLEANUP session list unavailable; falling back to locally tracked session");
        StopSession(session, stale_session_id);
    }
}
