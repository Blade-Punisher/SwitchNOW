GfnClient::GfnClient()
    : device_id_(NormalizeCloudDeviceId(GenerateDeviceId()))
{
    if (device_id_.empty())
        throw std::runtime_error("Unable to create a stable GeForce NOW device identity");
}

std::vector<StreamRegion> GfnClient::FetchStreamRegions(AuthSession& session) const
{
    session = RecoverSavedSession(session);
    const std::string base_url = server_location::ResolveStreamingBaseUrl(
        "Auto",
        session.provider.streaming_service_url);
    if (base_url.empty())
        throw std::runtime_error("The GeForce NOW provider has no valid streaming endpoint.");

    const HttpResponse response = http_client_.Get(
        base_url + "v2/serverInfo",
        kUserAgent,
        BuildGfnLcarsHeaders(
            ResolveSessionJwt(session),
            "BROWSER",
            "WEBRTC",
            true));
    if (response.status_code != 200)
    {
        throw std::runtime_error(
            "Could not load GeForce NOW server locations (HTTP " +
            std::to_string(response.status_code) + ").");
    }

    JsonPtr root = LoadJson(response.body);
    json_t* metadata = json_object_get(root.get(), "metaData");
    std::vector<StreamRegion> regions;
    std::unordered_set<std::string> seen_urls;
    size_t index = 0;
    json_t* entry = nullptr;
    json_array_foreach(metadata, index, entry)
    {
        const std::string name = Trim(GetString(entry, "key"));
        const std::string raw_url = Trim(GetString(entry, "value"));
        if (name.empty() || name == "gfn-regions" || name.rfind("gfn-", 0) == 0)
            continue;

        const std::string url =
            server_location::NormalizeStreamingBaseUrl(raw_url);
        if (url.empty() || !seen_urls.insert(url).second)
            continue;

        StreamRegion region;
        region.name = name; region.url = url;
        regions.push_back(std::move(region));
    }

    if (ZoneCatalogEligible(session)) {
        for (auto& zone : FetchServerZoneCatalog(http_client_)) {
            const auto found = std::find_if(regions.begin(), regions.end(), [&](const auto& r) { return r.url == zone.url; });
            if (found == regions.end()) regions.push_back(std::move(zone));
            else {
                found->zone_id = zone.zone_id; found->zone_queue_estimate = zone.zone_queue_estimate;
                found->zone_queue_at_ms = zone.zone_queue_at_ms; found->zone_catalog_at_ms = zone.zone_catalog_at_ms;
            }
        }
    }

    std::sort(
        regions.begin(),
        regions.end(),
        [](const StreamRegion& left, const StreamRegion& right) {
            return left.name < right.name;
        });
    const auto history = LoadCachedStreamRegions(session);
    for (StreamRegion& region : regions)
    {
        const auto previous = std::find_if(history.begin(), history.end(),
            [&region](const StreamRegion& item) { return item.url == region.url; });
        if (previous != history.end()) {
            region.observed_stream_rtt_ms = previous->observed_stream_rtt_ms;
            region.observed_stream_p95_ms = previous->observed_stream_p95_ms;
            region.observed_transport_pause_count = previous->observed_transport_pause_count;
            region.observed_transport_pause_ms = previous->observed_transport_pause_ms;
            region.observed_playback_pause_count = previous->observed_playback_pause_count;
            region.observed_playback_pause_ms = previous->observed_playback_pause_ms;
            region.observed_session_seconds = previous->observed_session_seconds;
            region.observed_at_ms = previous->observed_at_ms;
            region.observed_server_host = previous->observed_server_host;
            region.observed_media_endpoint = previous->observed_media_endpoint;
        }
    }
    return regions;
}

std::vector<StreamRegion> GfnClient::MeasureStreamRegionLatencies(
    std::vector<StreamRegion> regions, const std::string& provider_url) const
{
    if (regions.empty())
        return regions;

    std::unique_lock<std::mutex> sweep(network::sweep_mutex, std::try_to_lock);
    if (!sweep || !network::ProbeAllowed()) {
        for (auto& region : regions) region.probe_degraded = true;
        routing::Event(GetAppHome(), "probe_suppressed", {{"reason", !sweep ? "sweep_already_running" : "launch_priority_or_resource_cooldown"}});
        return regions;
    }
    const auto started = std::chrono::steady_clock::now();
    for (auto& region : regions) region.probe_degraded = false;
    std::vector<std::vector<int>> samples(regions.size());
    std::vector<int> attempted(regions.size(), 0);
    bool stopped = false;
    auto probe = [&](const std::vector<size_t>& indices, int count, long timeout,
                     const char* phase, std::chrono::steady_clock::time_point deadline) {
        for (auto index : indices) {
            if (stopped || !network::ProbeAllowed() || std::chrono::steady_clock::now() >= deadline) {
                stopped = true; break;
            }
            auto measured = MeasureRegionLatencySamples(http_client_, regions[index].url,
                count, timeout, phase, deadline, attempted[index]);
            samples[index].insert(samples[index].end(), measured.begin(), measured.end());
            if (!network::ProbeAllowed()) stopped = true;
        }
    };
    auto apply = [&] {
        for (size_t i = 0; i < regions.size(); ++i) {
            const auto estimate = server_location::EstimateLatency(samples[i], attempted[i]);
            regions[i].ping_ms = estimate.median_ms;
            regions[i].jitter_ms = estimate.jitter_ms;
            regions[i].selection_score_ms = estimate.selection_score_ms;
            regions[i].successful_probes = estimate.successful_probes;
            regions[i].attempted_probes = attempted[i];
        }
    };
    // No worldwide zone sweep or retry of every unreachable address. Reserve
    // sockets for fresh confirmation of the best provider-advertised regions.
    const auto all = server_probe::CoarseCandidates(regions, NowMs());
    probe(all, 1, 1000, "coarse_bounded", started + std::chrono::seconds(13));
    apply();
    auto regular = regions, zones = regions;
    for (size_t i = 0; i < regions.size(); ++i) {
        if (regions[i].zone_id.empty()) zones[i].ping_ms = -1;
        else regular[i].ping_ms = -1;
    }
    auto finalists = server_location::SelectLatencyFinalists(regular, 2, 50);
    const auto zone_finalists = server_location::SelectLatencyFinalists(zones, 1, 35);
    finalists.insert(finalists.end(), zone_finalists.begin(), zone_finalists.end());
    const auto coarse = regions;
    for (auto i : finalists) {
        samples[i].clear(); attempted[i] = 0;
        routing::Event(GetAppHome(), "finalist", {{"host", routing::Host(regions[i].url)},
            {"reason", "bounded_shortlist_fresh_confirmation"}});
    }
    probe(finalists, 2, 1000, "refine", started + std::chrono::seconds(17));
    const auto failed = std::count_if(finalists.begin(), finalists.end(),
        [&](size_t i) { return samples[i].empty() && attempted[i] > 0; });
    const auto tested = std::count_if(finalists.begin(), finalists.end(),
        [&](size_t i) { return attempted[i] > 0; });
    const bool common_failure = server_probe::CommonFailure(tested, failed) ||
        (tested >= 2 && failed == tested);
    apply();
    for (auto i : finalists)
        regions[i] = server_probe::Finish(coarse[i], samples[i], attempted[i], common_failure || stopped);
    routing::Event(GetAppHome(), "probe_confidence", {{"tested_finalists", std::to_string(tested)},
        {"failed_finalists", std::to_string(failed)}, {"shared_failure", common_failure ? "1" : "0"},
        {"fallback", common_failure || stopped ? "preserved_coarse_evidence" : "refined_measurements"}});
    for (size_t i = 0; i < regions.size(); ++i) {
        if (std::find(finalists.begin(), finalists.end(), i) == finalists.end() && regions[i].ping_ms >= 0)
            regions[i].selection_score_ms += 250;
    }
    routing::Event(GetAppHome(), "probe_budget", {{"elapsed_ms", std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count())},
        {"budget_ms", "17000"}, {"parallel_sockets", "1"},
        {"minimum_open_interval_ms", "350"}, {"global_max_per_minute", "32"},
        {"stopped", stopped ? "1" : "0"}});
    if (stopped) for (auto& region : regions) region.probe_degraded = true;
    ApplyRouteHistory(regions, provider_url);
    return regions;
}

std::vector<StreamRegion> GfnClient::LoadCachedStreamRegions(
    const AuthSession& session) const
{
    const std::string provider_url =
        server_location::NormalizeStreamingBaseUrl(
            session.provider.streaming_service_url);
    std::lock_guard<std::mutex> lock(g_region_selection_cache.mutex);
    const PersistedRegionSelection cache = LoadPersistentRegionSelection(
        provider_url, kRegionDisplayCacheLifetimeMs, ZoneCatalogEligible(session));
    return cache.valid ? cache.regions : std::vector<StreamRegion> {};
}

void GfnClient::SaveStreamRegionMeasurements(
    const AuthSession& session,
    const std::vector<StreamRegion>& regions) const
{
    const std::string provider_url =
        server_location::NormalizeStreamingBaseUrl(
            session.provider.streaming_service_url);
    const std::string selected_url =
        server_zone::Select(regions, LoadStreamSettings().region, NowMs());
    if (provider_url.empty() || selected_url.empty())
        return;

    std::string selected_name;
    const auto selected = std::find_if(
        regions.begin(), regions.end(), [&selected_url](const StreamRegion& region) {
            return server_location::NormalizeStreamingBaseUrl(region.url) ==
                selected_url;
        });
    if (selected != regions.end())
        selected_name = selected->name;

    std::lock_guard<std::mutex> lock(g_region_selection_cache.mutex);
    g_region_selection_cache.provider_url = provider_url;
    g_region_selection_cache.selected_url = selected_url;
    g_region_selection_cache.selected_name = selected_name;
    g_region_selection_cache.regions = regions;
    g_region_selection_cache.measured_at = std::chrono::steady_clock::now();
    g_region_selection_cache.provider_fallback = false;
    try
    {
        SavePersistentRegionSelection(
            provider_url, selected_url, selected_name, regions);
    }
    catch (const std::exception& e)
    {
        AppendSessionTraceLog(
            "REGION cache write failed error=" + std::string(e.what()));
    }
}

void GfnClient::RecordObservedStreamRtt(
    const AuthSession& session, const std::string& selected_region,
    const std::string& actual_server, const std::string& media_endpoint,
    int rtt_ms, int p95_ms, int transport_pauses,
    int transport_pause_ms, int session_seconds,
    int playback_pauses, int playback_pause_ms, const std::string& initial_server,
    const std::string& cloud_session_id, int low_display_ms, int audio_underruns) const
{
    if (rtt_ms < 1 || rtt_ms > 300 || session_seconds < 60 ||
        actual_server.empty() || selected_region.empty())
        return;
    const std::string provider_url =
        server_location::NormalizeStreamingBaseUrl(
            session.provider.streaming_service_url);
    if (provider_url.empty())
        return;
    std::lock_guard<std::mutex> lock(g_region_selection_cache.mutex);
    // Train requested -> actual routing on independent cloud sessions. Keep
    // allocation history separate from the verified actual-region metrics.
    try {
        const auto path = GetAppHome() + "/server_route_history.json";
        auto routes = server_route::ParseHistory(ReadTextFile(path));
        if (!cloud_session_id.empty()) {
            server_route::Observation row;
            row.provider = provider_url;
            row.requested = server_location::NormalizeStreamingBaseUrl(selected_region);
            row.actual = server_location::RouteHost(actual_server);
            row.session_id = cloud_session_id;
            row.rtt_ms = rtt_ms;
            row.p95_ms = p95_ms;
            row.seconds = session_seconds;
            row.transport_pauses = std::max(0, transport_pauses);
            row.transport_pause_ms = std::max(0, transport_pause_ms);
            row.media_endpoint = server_location::RouteHost(media_endpoint);
            row.playback_pauses = std::max(0, playback_pauses);
            row.playback_pause_ms = std::max(0, playback_pause_ms);
            row.low_display_ms = std::max(0, low_display_ms);
            row.audio_underruns = std::max(0, audio_underruns);
            row.at_ms = NowMs();
            row.redirected = !initial_server.empty() &&
                server_location::RouteHost(initial_server) != row.actual;
            server_route::Remember(routes, std::move(row));
            WriteTextFileAtomically(path, server_route::SerializeHistory(routes));
            const auto prediction = server_route::Predict(routes, provider_url, selected_region, 0, NowMs());
            routing::Event(GetAppHome(), "route_history", {{"requested_host", routing::Host(selected_region)},
                {"actual_host", routing::Host(actual_server)}, {"initial_host", routing::Host(initial_server)},
                {"sessions", std::to_string(prediction.samples)},
                {"redirected_sessions", std::to_string(prediction.redirects)},
                {"cloud_session", cloud_session_id}, {"media_host", routing::Host(media_endpoint)},
                {"observed_seconds", std::to_string(session_seconds)}, {"low_display_ms", std::to_string(low_display_ms)},
                {"playback_pauses", std::to_string(playback_pauses)}, {"audio_underruns", std::to_string(audio_underruns)}});
        }
        // A new allocation must affect the next Auto decision, even if no
        // unambiguous actual-region mapping exists (e.g. two Paris aliases).
        ApplyRouteHistory(g_region_selection_cache.regions, provider_url);
        if (!g_region_selection_cache.provider_fallback)
            g_region_selection_cache.measured_at = {};
    } catch (...) {
        routing::Event(GetAppHome(), "history_write_failed", {{"kind", "actual_node"}});
    }
    PersistedRegionSelection cache = LoadPersistentRegionSelection(
        provider_url, kRegionDisplayCacheLifetimeMs, ZoneCatalogEligible(session));
    if (!cache.valid)
        return;
    const std::string selected_url = server_location::NormalizeStreamingBaseUrl(selected_region);
    if (selected_url.empty() || actual_server.empty()) return;
    const auto index = server_location::ObservedRegionIndex(cache.regions, selected_url, actual_server);
    if (index == cache.regions.size()) {
        routing::Event(GetAppHome(), "history_saved", {{"requested_host", routing::Host(selected_url)},
            {"actual_host", routing::Host(actual_server)}, {"region_score_updated", "0"},
            {"reason", "unverified_region_mapping"}});
        return;
    }
    const auto match = cache.regions.begin() + index;
    match->observed_stream_rtt_ms = match->observed_stream_rtt_ms > 0 &&
        match->observed_server_host == actual_server &&
        server_location::HistoryWeight(*match, NowMs()) > 0
        ? (2 * match->observed_stream_rtt_ms + rtt_ms + 1) / 3
        : rtt_ms;
    match->observed_server_host = actual_server;
    match->observed_media_endpoint = media_endpoint;
    match->observed_stream_p95_ms = p95_ms >= rtt_ms && p95_ms <= 2000 ? p95_ms : -1;
    match->observed_transport_pause_count = std::max(0, transport_pauses);
    match->observed_transport_pause_ms = std::max(0, transport_pause_ms);
    match->observed_playback_pause_count = std::max(0, playback_pauses);
    match->observed_playback_pause_ms = std::max(0, playback_pause_ms);
    match->observed_session_seconds = std::max(0, session_seconds);
    match->observed_at_ms = NowMs();
    try
    {
        SavePersistentRegionSelection(provider_url, cache.selected_url,
                                      cache.selected_name, cache.regions, cache.measured_at_ms);
        g_region_selection_cache.regions = cache.regions;
        if (g_region_selection_cache.selected_url == match->url &&
            server_location::ObservedRouteNeedsProbe(*match, NowMs())) {
            g_region_selection_cache.measured_at = {};
            routing::Event(GetAppHome(), "cache_invalidated", {{"reason", "observed_playback_pauses"},
                {"host", routing::Host(selected_url)}});
            AppendSessionTraceLog("REGION cache invalidated reason=observed_playback_pauses; next Auto launch will remeasure");
        }
        AppendSessionTraceLog("REGION observed streamRttMs=" +
            std::to_string(rtt_ms) + " p95Ms=" + std::to_string(p95_ms) +
            " pauses/countMs=" + std::to_string(transport_pauses) + "/" +
            std::to_string(transport_pause_ms) + " playbackPauses/countMs=" +
            std::to_string(playback_pauses) + "/" + std::to_string(playback_pause_ms) +
            " requested=" + selected_url +
            " actualServer=" + actual_server + " mediaEndpoint=" + media_endpoint);
        routing::Event(GetAppHome(), "history_saved", {{"requested_host", routing::Host(selected_url)},
            {"scored_region_host", routing::Host(match->url)}, {"region_score_updated", "1"},
            {"actual_host", routing::Host(actual_server)}, {"media_host", routing::Host(media_endpoint)},
            {"rtt_ms", std::to_string(rtt_ms)}, {"p95_ms", std::to_string(p95_ms)},
            {"playback_pauses", std::to_string(playback_pauses)}, {"playback_pause_ms", std::to_string(playback_pause_ms)},
            {"session_seconds", std::to_string(session_seconds)},
            {"future_quality_penalty_ms", std::to_string(server_location::ObservedQualityPenalty(*match, NowMs()))}});
    }
    catch (const std::exception& e)
    {
        AppendSessionTraceLog("REGION observed RTT persistence failed error=" +
            std::string(e.what()));
    }
}

std::vector<LoginProvider> GfnClient::FetchLoginProviders() const
{
    const HttpResponse response = http_client_.Get(
        kServiceUrlsEndpoint,
        kUserAgent,
        {"Accept: application/json"});

    if (response.status_code != 200)
    {
        throw std::runtime_error(
            "Provider discovery failed with HTTP " + std::to_string(response.status_code));
    }

    JsonPtr root = LoadJson(response.body);
    json_t* gfn_service_info =
        json_object_get(root.get(), "gfnServiceInfo");
    json_t* endpoints =
        gfn_service_info ? json_object_get(gfn_service_info, "gfnServiceEndpoints") : nullptr;

    std::vector<LoginProvider> providers;
    if (json_is_array(endpoints))
    {
        size_t index = 0;
        json_t* entry = nullptr;
        json_array_foreach(endpoints, index, entry)
        {
            LoginProvider provider;
            provider.idp_id                = GetString(entry, "idpId");
            provider.code                  = GetString(entry, "loginProviderCode");
            provider.display_name          = GetString(entry, "loginProviderDisplayName");
            provider.streaming_service_url = EnsureTrailingSlash(GetString(entry, "streamingServiceUrl"));
            provider.priority              = GetInteger(entry, "loginProviderPriority", 0);

            if (provider.code == "BPC")
                provider.display_name = "bro.game";

            if (!provider.idp_id.empty() && !provider.streaming_service_url.empty())
                providers.push_back(provider);
        }
    }

    if (providers.empty())
        providers.push_back(DefaultProvider());

    std::sort(
        providers.begin(),
        providers.end(),
        [](const LoginProvider& left, const LoginProvider& right) {
            return left.priority < right.priority;
        });

    return providers;
}

std::vector<PublicGame> GfnClient::FetchPublicGames() const
{
    const HttpResponse response = http_client_.Get(
        kPublicCatalogEndpoint,
        kUserAgent,
        {"Accept: application/json"});

    if (response.status_code != 200)
    {
        throw std::runtime_error(
            "Public catalog fetch failed with HTTP " + std::to_string(response.status_code));
    }

    JsonPtr root = LoadJson(response.body);
    if (!json_is_array(root.get()))
        throw std::runtime_error("Public catalog payload is not a JSON array");

    std::vector<PublicGame> games;

    size_t index = 0;
    json_t* item = nullptr;
    json_array_foreach(root.get(), index, item)
    {
        const std::string status = GetString(item, "status");
        const std::string title  = GetString(item, "title");
        if (status != "AVAILABLE" || title.empty())
            continue;

        PublicGame game;
        game.id        = GetString(item, "id");
        game.title     = title;
        game.store     = InferStore(item);
        game.publisher = GetString(item, "publisher");
        game.developer = GetString(item, "developer");
        game.genres = ParseLabelArray(json_object_get(item, "genres"));
        game.image_url = BuildSteamImageUrl(GetString(item, "steamUrl"));
        game.poster_url = game.image_url;
        game.description = GetString(item, "description");
        if (game.description.empty())
            game.description = GetString(item, "longDescription");
        if (game.id.empty())
            game.id = game.title;

        games.push_back(std::move(game));
    }

    std::sort(
        games.begin(),
        games.end(),
        [](const PublicGame& left, const PublicGame& right) {
            return left.title < right.title;
        });

    return games;
}

std::vector<PublicGame> GfnClient::FetchStoreGames(AuthSession* session, const std::string& query,
    const std::function<void(std::vector<PublicGame>, bool)>& snapshot, bool force) const
{
    const auto revision=game_feed::Revision();
    constexpr const char* directory = "sdmc:/switch/SwitchNOW";
    const auto identity = session ? game_feed::OwnerKey(session->user.user_id,
        session->provider.code, session->provider.streaming_service_url) : std::string("public");
    const auto owner = game_feed::ScopeKey("store", identity);
    // Search results are transient: never replace the complete browse snapshot.
    auto cached = query.empty() ? gfn::feed::LoadCache(directory, owner) : std::optional<gfn::feed::CachedFeed>{};
    auto convert = [](const std::vector<GameInfo>& records) {
        std::vector<PublicGame> games; games.reserve(records.size());
        for (const auto& record : records) games.push_back(game_feed::ToPublic(record));
        return games;
    };
    if (cached && snapshot) snapshot(convert(cached->games), true);
    game_feed::LaunchGate gate;
    auto cancelled = [&] { return gate.Check(network::launch_count.load() != 0); };
    try {
        HttpCancellationScope priority(cancelled);
        if (cancelled()) throw game_feed::Deferred();
        if (cached && !force && game_feed::Fresh(cached->updated_at_ms,
                static_cast<std::int64_t>(NowMs()), game_feed::kPanelAgeMs))
            return convert(cached->games);
        auto games = session ? FetchCatalogGames(*session, query, cached ? std::function<void(std::vector<PublicGame>, bool)>{} : snapshot) : FetchPublicGames();
        if (cancelled()) throw game_feed::Deferred();
        if (query.empty()) {
            gfn::feed::CachedFeed result;
            result.scanned_at_ms = result.updated_at_ms = static_cast<std::int64_t>(NowMs());
            for (const auto& game : games) result.games.push_back(game_feed::ToRecord(game));
            EnsureAppHome();
            if (!gfn::feed::SaveCache(directory, owner, result,revision)) AppendAuthLog("store: cache_write_unavailable");
        }
        return games;
    } catch (...) {
        if (cancelled()) throw game_feed::Deferred();
        throw;
    }
}

std::vector<PublicGame> GfnClient::FetchShortcutArtwork(
    const AuthSession& session, const std::string& game_id) const
{
    if (game_id.empty()) return {};
    // A single existing metadata query for the exact game. No token refresh,
    // region discovery, library fetch, title search or pagination.
    const auto response = http_client_.Get(
        BuildAppMetadataUrl("NP-AMS-08", {game_id}), kUserAgent,
        BuildGraphQlHeaders(ResolveSessionJwt(session)), 2, 5, 512 * 1024);
    if (response.status_code != 200) return {};
    JsonPtr result = LoadJson(response.body);
    bool has_next = false;
    std::string cursor;
    return ParseCatalogPage(result, has_next, cursor);
}

std::vector<PublicGame> GfnClient::FetchCatalogGames(
    AuthSession& session, const std::string& search_query,
    const std::function<void(std::vector<PublicGame>, bool)>& snapshot) const
{
    session = RecoverSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string vpc_id = ResolveVpcId(http_client_, session);

    std::vector<PublicGame> games;
    std::unordered_set<std::string> seen_ids;
    std::string cursor;

    for (int page = 0; page < 3; ++page)
    {
        HttpResponse response = http_client_.Post(
            kGraphQlEndpoint,
            kUserAgent,
            BuildGraphQlPostHeaders(jwt_token),
            BuildCatalogRequestBody(vpc_id, search_query, cursor));

        if (response.status_code == 401)
        {
            session = RecoverSavedSession(session, true);
            jwt_token = ResolveSessionJwt(session);
            response = http_client_.Post(
                kGraphQlEndpoint,
                kUserAgent,
                BuildGraphQlPostHeaders(jwt_token),
                BuildCatalogRequestBody(vpc_id, search_query, cursor));
        }

        if (response.status_code != 200)
        {
            throw std::runtime_error(
                "Catalog browse failed with HTTP " + std::to_string(response.status_code));
        }

        JsonPtr root = LoadJson(response.body);
        bool has_next_page = false;
        std::string end_cursor;
        std::vector<PublicGame> page_games =
            ParseCatalogPage(root, has_next_page, end_cursor);
        for (PublicGame& game : page_games)
        {
            const std::string key = game.id.empty() ? game.title : game.id;
            if (seen_ids.insert(key).second)
                games.push_back(std::move(game));
        }

        if (snapshot && page == 0) snapshot(games, false);
        if (!has_next_page || end_cursor.empty() || end_cursor == cursor)
            break;
        cursor = std::move(end_cursor);
    }

    return games;
}

AuthSession GfnClient::Login(const LoginProvider& provider, const std::string& login_hint) const
{
    AppendAuthLog("auth: login started provider=" + provider.code);

    const PkcePair pkce = GeneratePkce();
    const std::string state = HexEncode(GenerateRandomBytes(32).data(), 32);
    const std::string auth_url = BuildAuthorizeUrl(provider, pkce, state, kRedirectUri, login_hint);
    const std::string final_url = RunBrowserLogin(auth_url);

    if (GetQueryValue(final_url, "state") != state)
        throw std::runtime_error("GeForce NOW login callback failed the OAuth state check");

    const std::string error = GetQueryValue(final_url, "error");
    if (!error.empty())
    {
        throw std::runtime_error("GeForce NOW login failed: " + error);
    }

    const std::string code = GetQueryValue(final_url, "code");
    if (code.empty())
        throw std::runtime_error("The GeForce NOW login callback did not include an authorization code");

    NativeAuthHttpSession token_http;
    WarmNvidiaTokenConnection(token_http);
    AuthTokens tokens = ExchangeAuthorizationCode(token_http, code, pkce.verifier);
    AppendAuthLog("auth: token exchange ok");
    RequestClientToken(token_http, tokens);

    AuthSession session;
    session.provider = provider;
    session.tokens   = tokens;
    session.user     = FetchUserInfo(token_http, tokens);
    session.last_refresh_at_ms = NowMs();
    RefreshMembershipTier(http_client_, session);
    AppendAuthLog("auth: user info ok user_id_present=" + std::string(session.user.user_id.empty() ? "no" : "yes"));
    return session;
}
