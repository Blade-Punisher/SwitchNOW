void GfnClient::MarkGameOwned(AuthSession& session,const std::string& app_id,const std::string& variant_id) const
{ SetGameOwnership(session,app_id,variant_id,true); }
void GfnClient::MarkGameUnowned(AuthSession& session,const std::string& app_id,const std::string& variant_id) const
{ SetGameOwnership(session,app_id,variant_id,false); }

void GfnClient::SetGameOwnership(AuthSession& session,const std::string& app_id,const std::string& variant_id,bool owned) const
{
    const auto body=gfn::ownership::RequestBody(variant_id,owned);
    if(app_id.empty())throw std::runtime_error("Game ID is unavailable. Refresh the catalog.");
    const auto identity=game_feed::OwnerKey(session.user.user_id,session.provider.code,session.provider.streaming_service_url);
    if(identity.empty())throw std::runtime_error("Sign in to GFN before adding games to your library.");
    session=RecoverSavedSession(session);
    auto fetch=[&]{
        if(identity!=game_feed::OwnerKey(session.user.user_id,session.provider.code,session.provider.streaming_service_url))
            throw std::runtime_error("GFN account changed. Reopen the game card and try again.");
        return http_client_.Request("POST",kGraphQlEndpoint,kUserAgent,
        BuildGraphQlPostHeaders(ResolveSessionJwt(session)),body,5,20,512*1024);};
    auto response=fetch();
    if(response.status_code==401){session=RecoverSavedSession(session,true);response=fetch();}
    if(response.status_code!=200)throw std::runtime_error("GFN ownership request failed with HTTP "+std::to_string(response.status_code));
    gfn::ownership::ValidateResponse(response.body,app_id,owned);
    // Only an acknowledged mutation invalidates browse data; timed-out writes
    // are not blindly retried and never become optimistic ownership.
    gfn::feed::InvalidateAccount("sdmc:/switch/SwitchNOW",identity);
    AppendAuthLog(owned?"library: add_owned_variant confirmed":"library: remove_owned_variant confirmed");
}

std::vector<GameInfo> GfnClient::FetchLibraryGames(AuthSession& session, bool* complete,
    const std::function<void(std::vector<GameInfo>, bool)>& snapshot, bool force_full) const
{
    const auto revision=game_feed::Revision();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    if (complete) *complete = false;
    constexpr const char* directory = "sdmc:/switch/SwitchNOW";
    const std::string original_user = session.user.user_id;
    const std::string owner = game_feed::ScopeKey("library", game_feed::OwnerKey(original_user, session.provider.code, session.provider.streaming_service_url));
    auto cached = gfn::feed::LoadCache(directory, owner);
    auto publish = [&](std::vector<GameInfo> games, bool full) {
        ApplyPlayHistory(games, LoadPlayHistory());
        if (snapshot) snapshot(std::move(games), full);
    };
    if (cached) {
        publish(cached->games, true);
        AppendAuthLog("library: disk_snapshot games=" + std::to_string(cached->games.size()));
    }
    game_feed::LaunchGate gate;
    auto yield_to_launch = [&] { return gate.Check(network::launch_count.load() != 0); };
    try {
        HttpCancellationScope launch_priority([&]{return yield_to_launch() || std::chrono::steady_clock::now()>=deadline;});
        if (yield_to_launch()) throw game_feed::Deferred();
        const auto now = static_cast<std::int64_t>(NowMs());
        if (cached && !force_full && game_feed::Fresh(cached->updated_at_ms, now, game_feed::kPanelAgeMs) &&
            !game_feed::ScanRequired(false, true, cached->scanned_at_ms, now)) {
            if (complete) *complete = true;
            ApplyPlayHistory(cached->games, LoadPlayHistory());
            return std::move(cached->games);
        }
        session = RecoverSavedSession(session);
        std::string jwt_token = ResolveSessionJwt(session);
        const std::string vpc_id = ResolveVpcId(http_client_, session);
        std::vector<GameInfo> panel;
        bool panel_ok = false;
        try {
            auto response = http_client_.Get(BuildLibraryUrl(vpc_id, true), kUserAgent, BuildGraphQlHeaders(jwt_token),3,10,4*1024*1024);
            if (response.status_code == 401) {
                session = RecoverSavedSession(session, true);
                jwt_token = ResolveSessionJwt(session);
                response = http_client_.Get(BuildLibraryUrl(vpc_id, true), kUserAgent, BuildGraphQlHeaders(jwt_token),3,10,4*1024*1024);
            }
            if (response.status_code != 200)
                response = http_client_.Get(BuildLibraryUrl(vpc_id, false), kUserAgent, BuildGraphQlHeaders(jwt_token),3,10,4*1024*1024);
            if (response.status_code != 200)
                throw std::runtime_error("Library panel HTTP " + std::to_string(response.status_code));
            auto root = LoadJson(response.body);
            panel = ParseLibraryGames(root); panel_ok = true;
            if (!cached && !panel.empty()) publish(panel, false); // first cards before full scan/metadata
        } catch (const std::exception&) {
            if (yield_to_launch()) throw game_feed::Deferred();
            AppendAuthLog("library: panel unavailable; preserving snapshot or enumerating catalog");
        }
        auto merge = [](std::vector<GameInfo>& games, const std::vector<GameInfo>& incoming) {
            std::unordered_map<std::string, size_t> by_id;
            for (size_t i = 0; i < games.size(); ++i) by_id[games[i].id] = i;
            for (const auto& game : incoming) {
                auto found = by_id.find(game.id);
                if (found == by_id.end()) { by_id.emplace(game.id, games.size()); games.push_back(game); }
                else MergeGame(games[found->second], game);
            }
        };
        auto enrich_missing = [&](std::vector<GameInfo>& games) {
            std::vector<GameInfo> missing;
            for(const auto& game:games)if(game_feed::NeedsLibraryMetadata(game))missing.push_back(game);
            if(missing.empty())return;
            const auto metadata_deadline=std::min(deadline,std::chrono::steady_clock::now()+std::chrono::seconds(15));
            try {
                HttpCancellationScope budget([&]{return std::chrono::steady_clock::now()>=metadata_deadline;});
                EnrichGamesWithMetadata(http_client_,jwt_token,vpc_id,missing);
            } catch(...) {if(yield_to_launch())throw game_feed::Deferred();}
            merge(games,missing); // Keep successfully enriched chunks on timeout.
        };
        const auto scan_required = game_feed::ScanRequired(force_full, cached.has_value(),
            cached ? cached->scanned_at_ms : 0, now);
        if (!scan_required && panel_ok) {
            auto games = cached->games;
            merge(games,panel);
            publish(games,true); // Existing full membership stays usable during detail refresh.
            enrich_missing(games);
            if (yield_to_launch()) throw game_feed::Deferred();
            if (session.user.user_id == original_user) {
                EnsureAppHome();
                gfn::feed::SaveCache(directory, owner, {games, cached->scanned_at_ms, static_cast<std::int64_t>(NowMs())},revision);
            }
            if (complete) *complete = true;
            ApplyPlayHistory(games, LoadPlayHistory());
            AppendAuthLog("library: cached_complete panel_refresh full_scan=skipped games=" + std::to_string(games.size()));
            return games;
        }
        // A failed panel check cannot turn an old snapshot into a fresh one.
        if (!scan_required && !panel_ok) {
            ApplyPlayHistory(cached->games, LoadPlayHistory());
            return std::move(cached->games);
        }
        std::vector<GameInfo> provisional = panel;
        size_t published_owned = 0;
        const auto operation = BeginRuntimeOperation("catalog", "library_complete", "panel_games=" + std::to_string(panel.size()));
        try {
            HttpCancellationScope cancellation([&] { return yield_to_launch() || std::chrono::steady_clock::now() >= deadline; });
            bool auth_refreshed = false;
            const auto result = gfn::library::Scan([&](const std::string& cursor) {
                auto fetch = [&] {
                    return http_client_.Request("POST", kGraphQlEndpoint, kUserAgent,
                        BuildGraphQlPostHeaders(jwt_token), gfn::library::RequestBody(vpc_id, cursor),
                        3, 10, 4 * 1024 * 1024);
                };
                auto response = fetch();
                if (response.status_code == 401 && !auth_refreshed) {
                    auth_refreshed = true; session = RecoverSavedSession(session, true);
                    jwt_token = ResolveSessionJwt(session); response = fetch();
                }
                if (response.status_code != 200)
                    throw std::runtime_error("Library catalog HTTP " + std::to_string(response.status_code));
                return response.body;
            }, [&](const gfn::library::ScanResult& progress) {
                if (cached || progress.owned_apps.empty() ||
                    (published_owned != 0 && progress.owned_apps.size() < published_owned + 15)) return;
                if (yield_to_launch()) throw game_feed::Deferred();
                auto preview = panel;
                std::vector<GameInfo> owned;
                for (const auto& app : progress.owned_apps) {
                    auto root = LoadJson(app); auto game = ParseApp(root.get());
                    if (!game.id.empty() && !game.title.empty()) owned.push_back(std::move(game));
                }
                merge(preview, owned);
                provisional = preview; published_owned = progress.owned_apps.size();
                AppendAuthLog("library: scan_preview pages=" + std::to_string(progress.pages) +
                    " owned=" + std::to_string(published_owned));
                publish(std::move(preview), false);
            });
            std::vector<GameInfo> games = panel, owned;
            for (const auto& app : result.owned_apps) {
                auto root = LoadJson(app); auto game = ParseApp(root.get());
                if (!game.id.empty() && !game.title.empty()) owned.push_back(std::move(game));
            }
            merge(games, owned);
            if(cached)game_feed::ReuseLibraryMetadata(games,cached->games);
            // Enumeration establishes complete membership. Publish/cache it before
            // optional descriptions/screenshots, so launch cancellation cannot
            // discard a completed scan and force the next launch to start over.
            if(yield_to_launch())throw game_feed::Deferred();
            const auto scanned_at=static_cast<std::int64_t>(NowMs());
            if(session.user.user_id==original_user) {
                EnsureAppHome();
                gfn::feed::SaveCache(directory,owner,{games,scanned_at,scanned_at},revision);
            }
            publish(games,true);
            AppendAuthLog("library: membership_ready games="+std::to_string(games.size()));
            enrich_missing(games);
            if (yield_to_launch()) throw game_feed::Deferred();
            const auto refreshed_at = static_cast<std::int64_t>(NowMs());
            if (session.user.user_id == original_user) {
                EnsureAppHome();
                if (!gfn::feed::SaveCache(directory, owner, {games, refreshed_at, refreshed_at},revision))
                    AppendAuthLog("library: cache_write_unavailable");
            }
            if (complete) *complete = true;
            EndRuntimeOperation(operation, "catalog", "library_complete", "ok",
                "pages=" + std::to_string(result.pages) + " scanned=" + std::to_string(result.scanned) +
                " owned=" + std::to_string(result.owned_apps.size()) + " merged=" + std::to_string(games.size()));
            ApplyPlayHistory(games, LoadPlayHistory()); return games;
        } catch (const std::exception& error) {
            EndRuntimeOperation(operation, "catalog", "library_complete", gate.deferred() ? "deferred" : "partial", error.what());
            if (yield_to_launch()) throw game_feed::Deferred();
            if (cached) { ApplyPlayHistory(cached->games, LoadPlayHistory()); return std::move(cached->games); }
            if (!provisional.empty()) { ApplyPlayHistory(provisional, LoadPlayHistory()); return provisional; }
            if (!panel_ok) throw;
        }
        ApplyPlayHistory(panel, LoadPlayHistory()); return panel;
    } catch (...) {
        if (yield_to_launch()) throw game_feed::Deferred();
        throw;
    }
}

bool GfnClient::LoadSavedSession(AuthSession& session) const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::string active_user_id;
    std::vector<AuthSession> sessions = LoadAccountsFromDisk(&active_user_id);
    if (!sessions.empty())
    {
        for (const AuthSession& saved : sessions)
        {
            if (saved.user.user_id == active_user_id)
            {
                session = saved;
                AppendAuthLog("auth: selected saved account " + AuthSessionHealth(session));
                return true;
            }
        }

        session = sessions.front();
        SaveAccountsToDisk(sessions, session.user.user_id);
        AppendAuthLog("auth: selected fallback account " + AuthSessionHealth(session));
        return true;
    }

    if (LoadLegacySession(session))
    {
        SaveSession(session);
        AppendAuthLog("auth: selected migrated legacy account " + AuthSessionHealth(session));
        return true;
    }

    return false;
}

std::vector<AuthSession> GfnClient::LoadSavedSessions() const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::vector<AuthSession> sessions = LoadAccountsFromDisk();
    if (!sessions.empty())
        return sessions;

    AuthSession legacy;
    if (LoadLegacySession(legacy))
    {
        SaveSession(legacy);
        sessions.push_back(std::move(legacy));
    }

    return sessions;
}

void GfnClient::SaveSession(const AuthSession& session) const
{
    if (!session.persistence_enabled)
        return;

    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    EnsureAppHome();

    std::vector<AuthSession> sessions = LoadAccountsFromDisk();
    bool replaced = false;
    for (AuthSession& saved : sessions)
    {
        if (saved.user.user_id == session.user.user_id)
        {
            saved = session;
            replaced = true;
            break;
        }
    }

    if (!replaced)
        sessions.push_back(session);

    SaveAccountsToDisk(sessions, session.user.user_id);
}

bool GfnClient::SetActiveSavedSession(const std::string& user_id) const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::vector<AuthSession> sessions = LoadAccountsFromDisk();
    for (const AuthSession& session : sessions)
    {
        if (session.user.user_id == user_id)
        {
            SaveAccountsToDisk(sessions, session.user.user_id);
            return true;
        }
    }

    return false;
}

void GfnClient::ClearSavedSession() const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    AuthSession active;
    if (!LoadSavedSession(active))
    {
        std::remove(GetSessionPath().c_str());
        std::remove((GetSessionPath() + ".bak").c_str());
        return;
    }

    std::vector<AuthSession> sessions = LoadAccountsFromDisk();
    sessions.erase(
        std::remove_if(
            sessions.begin(),
            sessions.end(),
            [&active](const AuthSession& session) {
                return session.user.user_id == active.user.user_id;
            }),
        sessions.end());

    if (sessions.empty())
    {
        std::remove(GetAccountsPath().c_str());
        std::remove((GetAccountsPath() + ".bak").c_str());
        std::remove(GetSessionPath().c_str());
        std::remove((GetSessionPath() + ".bak").c_str());
        return;
    }

    SaveAccountsToDisk(sessions, sessions.front().user.user_id);
}

void GfnClient::ClearAllSavedSessions() const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::remove(GetAccountsPath().c_str());
    std::remove((GetAccountsPath() + ".bak").c_str());
    std::remove(GetSessionPath().c_str());
    std::remove((GetSessionPath() + ".bak").c_str());
}

std::optional<NativeCredentials> GfnClient::LoadNativeCredentials(
    const std::string& provider_id) const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    for (NativeCredentials& credentials : LoadNativeCredentialEntries())
    {
        if (credentials.provider_id == provider_id)
            return credentials;
    }
    return std::nullopt;
}

void GfnClient::SaveNativeCredentials(const NativeCredentials& credentials) const
{
    if (credentials.provider_id.empty() || credentials.email.empty() || credentials.password.empty())
        throw std::runtime_error("Cannot save incomplete NVIDIA credentials");
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::vector<NativeCredentials> entries = LoadNativeCredentialEntries();
    bool replaced = false;
    for (NativeCredentials& saved : entries)
    {
        if (saved.provider_id == credentials.provider_id)
        {
            saved = credentials;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        entries.push_back(credentials);
    SaveNativeCredentialEntries(entries);
}

void GfnClient::ClearNativeCredentials(const std::string& provider_id) const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::vector<NativeCredentials> entries = LoadNativeCredentialEntries();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](const NativeCredentials& entry) {
            return entry.provider_id == provider_id;
        }),
        entries.end());
    SaveNativeCredentialEntries(entries);
    AppendAuthLog("auth-native: saved password removed");
}

void GfnClient::ClearAllNativeCredentials() const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    SaveNativeCredentialEntries({});
    AppendAuthLog("auth-native: all saved passwords removed");
}

std::string GfnClient::LoadLauncherPreference(
    const std::string& user_id, const std::string& game_id) const
{
    if (user_id.empty() || game_id.empty())
        return "";
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    try {
        const std::string stored = ReadTextFile(GetLauncherPreferencesPath());
        if (stored.empty())
            return "";
        JsonPtr root = LoadJson(stored);
        return GetString(root.get(), (user_id + ":" + game_id).c_str());
    } catch (const std::exception& e) {
        AppendAuthLog("launcher: preference load failed error=" + std::string(e.what()));
        return "";
    }
}

void GfnClient::SaveLauncherPreference(
    const std::string& user_id, const std::string& game_id,
    const std::string& variant_id) const
{
    if (user_id.empty() || game_id.empty() || variant_id.empty())
        return;
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    try {
        JsonPtr root(json_object(), &json_decref);
        const std::string stored = ReadTextFile(GetLauncherPreferencesPath());
        if (!stored.empty()) {
            try {
                JsonPtr loaded = LoadJson(stored);
                if (json_is_object(loaded.get()))
                    root = std::move(loaded);
            } catch (...) {
            }
        }
        json_object_set_new(root.get(), (user_id + ":" + game_id).c_str(),
                            json_string(variant_id.c_str()));
        WriteJsonToFile(GetLauncherPreferencesPath(), root.get());
        AppendAuthLog("launcher: preference saved game=" + game_id + " variant=" + variant_id);
    } catch (const std::exception& e) {
        AppendAuthLog("launcher: preference save failed error=" + std::string(e.what()));
    }
}

[[maybe_unused]] static std::string GenerateUUID()
{
    auto bytes = GenerateRandomBytes(16);
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    std::string hex = HexEncode(bytes.data(), 16);
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" + hex.substr(20);
}

std::string SessionInfoTraceSummary(const SessionInfo& info)
{
    std::ostringstream out;
    out << "sessionId=" << info.session_id
        << " status=" << info.status
        << " queuePosition=" << info.queue_position
        << " queueSource=" << info.queue_source
        << " queueConflict=" << (info.queue_conflict ? 1 : 0)
        << " seatSetupStep=" << info.seat_setup_step
        << " appPatching=" << (info.app_patching ? 1 : 0)
        << " serverIp=" << info.server_ip
        << " requestedRegion=" << info.requested_region_url
        << " assignedServerId=" << info.assigned_server_id
        << " signalingUrl=" << info.signaling_url
        << " media=" << info.media_ip << ":" << info.media_port
        << " iceServers=" << info.ice_servers.size()
        << " sessionToken=" << (info.session_token.empty() ? "missing" : "present")
        << " adsRequired=" << (info.ad_break.required ? 1 : 0)
        << " ads=" << info.ad_break.ads.size()
        << " adDurationSec=" << ad_break::TotalDurationSeconds(info.ad_break);
    if (const auto* media = ad_break::SelectPreferredMedia(info.ad_break))
        out << " adMediaProfile=" << media->encoding_profile;
    return out.str();
}
