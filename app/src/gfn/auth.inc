AuthSession GfnClient::LoginNative(
    const LoginProvider& provider,
    const std::string& email,
    const std::string& password,
    const std::function<std::string(const std::string&)>& request_one_time_code,
    const std::function<void(const std::string&)>& report_status,
    const std::function<bool()>& is_cancelled) const
{
    if (Trim(email).empty() || password.empty())
        throw std::runtime_error("Email and password are required");

    AppendAuthLog("auth-native: login started provider=" + provider.code);
    const PkcePair pkce = GeneratePkce();
    const std::string state = HexEncode(GenerateRandomBytes(32).data(), 32);
    const std::string auth_url = BuildAuthorizeUrl(provider, pkce, state, kRedirectUri, Trim(email));
    NativeAuthHttpSession native_http;

    std::string current_url = auth_url;
    std::string key;
    for (int redirect = 0; redirect < 12; ++redirect)
    {
        if (current_url.rfind("https://login.nvgs.nvidia.com/", 0) == 0)
        {
            key = GetQueryValue(current_url, "key");
            if (!key.empty())
                break;
        }
        const NativeAuthResponse response = native_http.Request("GET", current_url);
        if (response.location.empty())
            break;
        current_url = ResolveRedirectUrl(current_url, response.location);
    }
    if (key.empty())
        throw NativeLoginFallbackRequired("NVIDIA did not offer its native password sign-in flow");
    AppendAuthLog("auth-native: OAuth context initialized");

    JsonPtr account_body(json_object(), &json_decref);
    json_object_set_new(account_body.get(), "email", json_string(Trim(email).c_str()));
    json_object_set_new(account_body.get(), "rememberLogin", json_true());
    json_object_set_new(account_body.get(), "autoLogin", json_false());
    json_object_set_new(account_body.get(), "deviceId", json_string(GenerateDeviceId().c_str()));
    JsonPtr account = NativeJsonRequest(
        native_http, "POST",
        "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/account/check",
        key, account_body.get(), true);
    key = GetString(account.get(), "key");
    if (key.empty())
        throw std::runtime_error("NVIDIA account check did not return a login context");

    NativeStage stage = AdvanceNativeStage(native_http, key);
    bool password_submitted = false;
    for (int transitions = 0; transitions < 24; ++transitions)
    {
        if (is_cancelled && is_cancelled())
            throw std::runtime_error("NVIDIA sign-in was cancelled");
        const auth::NativeStageAction stage_action = auth::ClassifyNativeStage(stage.page);
        if (stage_action == auth::NativeStageAction::Password)
        {
            if (password_submitted)
                throw std::runtime_error("NVIDIA rejected the supplied account credentials");
            JsonPtr body(json_object(), &json_decref);
            json_object_set_new(body.get(), "password", json_string(password.c_str()));
            json_object_set_new(body.get(), "rememberLogin", json_true());
            json_object_set_new(body.get(), "deviceId", json_string(GenerateDeviceId().c_str()));
            JsonPtr result = NativeJsonRequest(
                native_http, "POST",
                "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/user/login/password",
                stage.key, body.get(), true);
            key = GetString(result.get(), "key");
            if (key.empty())
                throw std::runtime_error("NVIDIA password login did not return a login context");
            password_submitted = true;
            AppendAuthLog("auth-native: password accepted");
            stage = AdvanceNativeStage(native_http, key);
            continue;
        }

        if (stage_action == auth::NativeStageAction::SelectMfa)
        {
            const std::string capability = UrlEncode("[\"TOTP\"]");
            JsonPtr factors = NativeJsonRequest(
                native_http, "GET",
                "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/auth/factor/challenge/factors"
                "?browserCapability=" + capability,
                stage.key, nullptr);
            json_t* values = json_object_get(factors.get(), "values");
            if (!json_is_array(values))
            {
                json_t* nested = json_object_get(factors.get(), "factors");
                values = json_is_object(nested) ? json_object_get(nested, "values") : nested;
            }
            json_t* selected_totp = nullptr;
            json_t* selected_email = nullptr;
            std::vector<std::string> factor_types;
            if (json_is_array(values))
            {
                size_t index = 0;
                json_t* item = nullptr;
                json_array_foreach(values, index, item)
                {
                    const std::string type = GetString(item, "type");
                    const std::string normalized = Lowercase(type);
                    if (!type.empty())
                        factor_types.push_back(type);
                    if (normalized == "totp")
                        selected_totp = item;
                    else if (normalized == "email" || normalized == "emailcode")
                        selected_email = item;
                }
            }
            std::string factor_summary;
            for (const std::string& type : factor_types)
            {
                if (!factor_summary.empty())
                    factor_summary += ",";
                factor_summary += type;
            }
            AppendAuthLog(
                "auth-native: MFA factors count=" + std::to_string(factor_types.size()) +
                " types=" + (factor_summary.empty() ? std::string("none") : factor_summary));
            json_t* selected = selected_totp ? selected_totp : selected_email;
            if (!selected)
                throw NativeLoginFallbackRequired(
                    "This account requires a security key, passkey, email approval, or another unsupported verification method");

            std::string factor_id = GetString(selected, "id");
            json_t* descriptors = json_object_get(selected, "descriptors");
            if (factor_id.empty() && json_is_array(descriptors) && json_array_size(descriptors) > 0)
                factor_id = GetString(json_array_get(descriptors, 0), "id");

            const std::string selected_type = GetString(selected, "type");
            JsonPtr init_body(json_object(), &json_decref);
            json_object_set_new(init_body.get(), "type", json_string(selected_type.c_str()));
            if (factor_id.empty())
                json_object_set_new(init_body.get(), "id", json_null());
            else
                json_object_set_new(init_body.get(), "id", json_string(factor_id.c_str()));
            JsonPtr initialized = NativeJsonRequest(
                native_http, "POST",
                "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/auth/factor/challenge/initialize",
                stage.key, init_body.get());
            key = GetString(initialized.get(), "key");
            if (key.empty())
                throw std::runtime_error("NVIDIA MFA initialization did not return a login context");
            AppendAuthLog("auth-native: MFA initialized type=" + selected_type);
            stage = AdvanceNativeStage(native_http, key);
            continue;
        }

        if (stage_action == auth::NativeStageAction::VerifyTotp)
        {
            if (!request_one_time_code)
                throw NativeLoginFallbackRequired("A NVIDIA authenticator code is required");
            const std::string otp = Trim(request_one_time_code("Enter the 6-digit NVIDIA authenticator code"));
            if (otp.empty())
                throw std::runtime_error("NVIDIA verification was cancelled");
            JsonPtr verify_body(json_object(), &json_decref);
            json_object_set_new(verify_body.get(), "otp", json_string(otp.c_str()));
            JsonPtr verified = NativeJsonRequest(
                native_http, "POST",
                "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/auth/factor/challenge/verify",
                stage.key, verify_body.get());
            key = GetString(verified.get(), "key");
            if (key.empty())
                throw std::runtime_error("NVIDIA MFA verification did not return a login context");
            AppendAuthLog("auth-native: TOTP accepted");
            stage = AdvanceNativeStage(native_http, key);
            continue;
        }

        if (stage_action == auth::NativeStageAction::WaitForEmail)
        {
            if (report_status)
                report_status("NVIDIA sent an approval email. Open it and approve this sign-in.");
            AppendAuthLog("auth-native: waiting for email approval");
            bool approved = false;
            for (int attempt = 1; attempt <= 60; ++attempt)
            {
                if (is_cancelled && is_cancelled())
                    throw std::runtime_error("NVIDIA sign-in was cancelled");
                if (report_status && (attempt == 1 || attempt % 5 == 0))
                {
                    const int elapsed_seconds = (attempt - 1) * 3;
                    report_status(
                        "NVIDIA sent an approval email. Open it and approve this sign-in. "
                        "Waiting: " + std::to_string(elapsed_seconds) +
                        " seconds (timeout: 180 seconds). Check Spam if needed.");
                }
                const NativeAuthResponse response = native_http.Request(
                    "GET",
                    "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/user/email/auth/poll",
                    NativeAuthHeaders(stage.key));
                if (response.status_code == 200)
                {
                    JsonPtr result = LoadJson(response.body);
                    key = GetString(result.get(), "key");
                    if (key.empty())
                        throw std::runtime_error("NVIDIA email approval returned no login context");
                    AppendAuthLog("auth-native: email approval accepted attempts=" + std::to_string(attempt));
                    stage = AdvanceNativeStage(native_http, key);
                    approved = true;
                    break;
                }
                if (response.status_code == 202 || response.status_code == 400 ||
                    response.status_code == 404 || response.status_code == 429 ||
                    response.status_code >= 500)
                {
                    if (attempt == 1 || attempt % 10 == 0)
                    {
                        AppendAuthLog(
                            "auth-native: email approval pending HTTP=" +
                            std::to_string(response.status_code) +
                            " attempt=" + std::to_string(attempt));
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    continue;
                }
                throw std::runtime_error(
                    "NVIDIA email approval failed with HTTP " +
                    std::to_string(response.status_code));
            }
            if (!approved)
                throw std::runtime_error("NVIDIA email approval timed out after 3 minutes");
            continue;
        }

        if (stage_action == auth::NativeStageAction::Consent)
        {
            JsonPtr consent(json_object(), &json_decref);
            JsonPtr accepted = NativeJsonRequest(
                native_http, "POST",
                "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/user/consent",
                stage.key, consent.get());
            key = GetString(accepted.get(), "key");
            if (key.empty())
                throw NativeLoginFallbackRequired("NVIDIA requires an interactive consent screen");
            stage = AdvanceNativeStage(native_http, key);
            continue;
        }

        if (stage_action == auth::NativeStageAction::Advance)
        {
            stage = AdvanceNativeStage(native_http, stage.key);
            continue;
        }

        if (stage_action == auth::NativeStageAction::Finish)
        {
            current_url = stage.external_url.empty()
                ? "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/user/next?key=" +
                    UrlEncode(stage.key) + "&state=finish"
                : stage.external_url;
            std::string final_url;
            for (int redirect = 0; redirect < 16; ++redirect)
            {
                if (current_url.rfind(kRedirectUri, 0) == 0)
                {
                    final_url = current_url;
                    break;
                }
                const NativeAuthResponse response = native_http.Request("GET", current_url);
                if (response.location.empty())
                    break;
                current_url = ResolveRedirectUrl(current_url, response.location);
            }
            if (final_url.empty())
                throw std::runtime_error("NVIDIA login completed without an OAuth callback");
            if (GetQueryValue(final_url, "state") != state)
                throw std::runtime_error("NVIDIA native login failed the OAuth state check");
            const std::string error = GetQueryValue(final_url, "error");
            if (!error.empty())
                throw std::runtime_error("NVIDIA native login failed: " + error);
            const std::string code = GetQueryValue(final_url, "code");
            if (code.empty())
                throw std::runtime_error("NVIDIA native login did not return an authorization code");

            // Keep the working NVIDIA TLS connection from authorize through token,
            // client-token and user-info. Opening a second libnx curl handle here
            // intermittently fails before HTTP on current login.nvidia.com edges.
            AuthTokens tokens = ExchangeAuthorizationCode(native_http, code, pkce.verifier);
            RequestClientToken(native_http, tokens);
            AuthSession auth_session;
            auth_session.provider = provider;
            auth_session.tokens = std::move(tokens);
            auth_session.user = FetchUserInfo(native_http, auth_session.tokens);
            auth_session.last_refresh_at_ms = NowMs();
            RefreshMembershipTier(http_client_, auth_session);
            AppendAuthLog("auth-native: login complete");
            return auth_session;
        }

        if (stage.page == "Captcha")
            throw NativeLoginFallbackRequired("NVIDIA requires a CAPTCHA for this sign-in");

        throw NativeLoginFallbackRequired(
            "NVIDIA requires an interactive step not supported by native login: " +
            (stage.page.empty() ? std::string("unknown") : stage.page));
    }

    throw std::runtime_error("NVIDIA native login exceeded the stage transition limit");
}

class QrCallbackServer
{
  public:
    explicit QrCallbackServer(const std::string& auth_url) : auth_url_(auth_url) {}

    ~QrCallbackServer()
    {
        Stop();
    }

    void Start()
    {
        const int server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0)
            throw std::runtime_error("QR callback server socket() failed");

#ifndef _WIN32
        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse), sizeof(reuse));
#endif

        sockaddr_in address {};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(kLoginCallbackPort);
        address.sin_addr.s_addr = INADDR_ANY;

        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
        {
            close(server);
            throw std::runtime_error(std::string("QR callback server bind() failed on port ") + std::to_string(kLoginCallbackPort));
        }

        port_ = kLoginCallbackPort;

        if (listen(server, 1) < 0)
        {
            close(server);
            throw std::runtime_error("QR callback server listen() failed");
        }

        AppendAuthLog("auth: QR callback server listening on fixed port " + std::to_string(port_));

        worker_ = std::thread([this, server]() {
            Run(server);
        });
    }

    void Stop()
    {
        stop_ = true;
        if (worker_.joinable())
            worker_.join();
    }

    std::string WaitForResult(std::function<bool()> isCancelled)
    {
        while (!completed_ && !stop_)
        {
            if (isCancelled && isCancelled())
            {
                Stop();
                return "";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!error_.empty())
            throw std::runtime_error(error_);

        return callback_url_;
    }

    int port() const
    {
        return port_;
    }

  private:
    void Complete(std::string callback_url, std::string error = {})
    {
        callback_url_ = std::move(callback_url);
        error_        = std::move(error);
        completed_    = true;
    }

    void SendResponse(int client, bool ok)
    {
        const std::string body =
            ok
                ? "<!doctype html><html><body style=\"font-family:sans-serif;background:#0e1015;color:#fff;padding:32px;text-align:center;\"><h2>SwitchNOW login complete</h2><p style=\"color:#76b900\">You can close this tab and return to your Switch!</p></body></html>"
                : "<!doctype html><html><body style=\"font-family:sans-serif;background:#0e1015;color:#fff;padding:32px;text-align:center;\"><h2 style=\"color:red\">SwitchNOW login failed</h2><p>Please try again.</p></body></html>";

        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;

        send(client, response.data(), response.size(), 0);
    }

    void Run(int server)
    {
        while (!stop_)
        {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server, &read_fds);

            timeval timeout {};
            timeout.tv_sec = 1;

            const int ready = select(server + 1, &read_fds, nullptr, nullptr, &timeout);
            if (ready <= 0)
                continue;

            const int client = accept(server, nullptr, nullptr);
            if (client < 0)
                continue;

            char buffer[2048] {};
            const ssize_t read_bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
            if (read_bytes <= 0)
            {
                close(client);
                continue;
            }

            const std::string request(buffer, static_cast<size_t>(read_bytes));
            const auto first_space  = request.find(' ');
            const auto second_space = first_space == std::string::npos
                                          ? std::string::npos
                                          : request.find(' ', first_space + 1);

            std::string target =
                first_space == std::string::npos || second_space == std::string::npos
                    ? std::string("/")
                    : request.substr(first_space + 1, second_space - first_space - 1);

            if (target == "/")
            {
                std::string body = R"html(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: sans-serif; background: #0e1015; color: #fff; padding: 20px; text-align: center; }
.btn { display: inline-block; background: #76b900; color: #000; padding: 15px 30px; text-decoration: none; font-weight: bold; border-radius: 5px; margin: 20px 0; font-size: 18px; }
input { width: 100%; padding: 15px; box-sizing: border-box; margin: 10px 0; border: none; border-radius: 5px; font-size: 16px; color: #000; }
.instructions { text-align: left; background: #1a1e24; padding: 15px; border-radius: 5px; margin-bottom: 20px; font-size: 15px; line-height: 1.4; }
</style></head><body><h2>GeForce NOW Login</h2>
<div class="instructions"><p>This Switch login page always runs on port <b>2259</b>.</p><p>1. Tap the button below to log in to NVIDIA.</p><p>2. After logging in, your browser will show an error like "Site cannot be reached" for <b>localhost:2259</b>.</p>
<p>3. <b>Copy the full URL</b> from your browser's address bar.</p><p>4. Go back to this page and paste the URL below.</p></div>
<a href=")html" + auth_url_ + R"html(" target="_blank" class="btn">Login to NVIDIA</a>
<input type="text" id="pastedUrl" placeholder="Paste the localhost URL here...">
<a href="#" class="btn" onclick="submitUrl()">Submit</a>
<script>
function submitUrl() {
  var url = document.getElementById('pastedUrl').value;
  if (!url) return;
  window.location.href = '/callback?url=' + encodeURIComponent(url);
}
</script></body></html>)html";

                const std::string response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Connection: close\r\n"
                    "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;

                send(client, response.data(), response.size(), 0);
                close(client);
                continue;
            }

            if (target.find("/callback?url=") == 0)
            {
                std::string pasted_url = UrlDecode(target.substr(14));
                target = pasted_url;
            }

            const bool has_code  = !GetQueryValue(target, "code").empty();
            const bool has_error = !GetQueryValue(target, "error").empty();
            const bool is_callback = has_code || has_error;
            
            if (is_callback) {
                SendResponse(client, has_code && !has_error);
            } else {
                const std::string response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
                send(client, response.data(), response.size(), 0);
            }
            
            close(client);

            if (is_callback)
            {
                Complete(target);
                break;
            }
        }

        close(server);
    }

    std::atomic<bool> stop_ {false};
    std::thread worker_;
    std::atomic<bool> completed_ {false};
    std::string callback_url_;
    std::string error_;
    std::string auth_url_;
    int port_ = 0;
};

AuthSession GfnClient::ReauthenticateWithSavedCredentials(
    const AuthSession& expired_session,
    const std::function<void(const std::string&)>& report_status,
    const std::function<bool()>& is_cancelled) const
{
    // Store, Library and the periodic account check can notice the same
    // expired token together. Keep native login single-flight and reuse a
    // session another request has just restored.
    std::lock_guard<std::mutex> reauthentication_lock(g_reauthentication_mutex);
    for (const AuthSession& candidate : LoadSavedSessions())
    {
        const bool same_user = candidate.user.user_id == expired_session.user.user_id;
        const bool newer_login = candidate.last_refresh_at_ms > expired_session.last_refresh_at_ms;
        const bool different_token =
            candidate.tokens.access_token != expired_session.tokens.access_token;
        if (!same_user || !newer_login || !different_token)
            continue;

        try
        {
            AuthSession restored = EnsureFreshSavedSession(candidate);
            restored.reauthentication_required = false;
            AppendAuthLog("auth: reused quick sign-in completed by another request");
            return restored;
        }
        catch (const ReauthenticationRequired&)
        {
            // The newer token was rejected too; continue with the remembered
            // credentials while holding the single-flight lock.
        }
    }

    const std::optional<NativeCredentials> stored =
        LoadNativeCredentials(expired_session.provider.idp_id);
    if (!stored || stored->email.empty() || stored->password.empty())
    {
        throw ReauthenticationRequired(
            "The NVIDIA session expired and no password is saved for automatic sign-in.");
    }

    NativeCredentials credentials = *stored;
    AppendAuthLog("auth: automatic quick sign-in started provider=" +
                  expired_session.provider.code);
    try
    {
        AuthSession restored = LoginNative(
            expired_session.provider,
            credentials.email,
            credentials.password,
            [](const std::string&) { return std::string {}; },
            report_status,
            is_cancelled);
        std::fill(credentials.password.begin(), credentials.password.end(), '\0');
        restored.persistence_enabled = true;
        restored.reauthentication_required = false;
        SaveSession(restored);
        AppendAuthLog("auth: automatic quick sign-in completed user_id_present=" +
                      std::string(restored.user.user_id.empty() ? "no" : "yes"));
        return restored;
    }
    catch (...)
    {
        std::fill(credentials.password.begin(), credentials.password.end(), '\0');
        AppendAuthLog("auth: automatic quick sign-in failed");
        throw;
    }
}

AuthSession GfnClient::RecoverSavedSession(
    const AuthSession& session,
    bool force_refresh,
    const std::function<void(const std::string&)>& report_status,
    const std::function<bool()>& is_cancelled) const
{
    const bool can_refresh_tokens = auth::SelectRefreshMechanism(
        !session.tokens.client_token.empty(),
        !session.user.user_id.empty(),
        !session.tokens.refresh_token.empty()) != auth::RefreshMechanism::None;

    if (session.reauthentication_required && !can_refresh_tokens)
    {
        if (report_status)
            report_status("Restoring the remembered NVIDIA account.");
        return ReauthenticateWithSavedCredentials(session, report_status, is_cancelled);
    }

    try
    {
        // Older builds persisted reauthentication_required when they failed to use a
        // perfectly valid client token. Treat that flag as a forced token check first,
        // and only fall back to credentials if NVIDIA actually rejects the refresh.
        AuthSession recovered = (force_refresh || session.reauthentication_required)
            ? ForceRefreshSavedSession(session)
            : EnsureFreshSavedSession(session);
        recovered.reauthentication_required = false;
        return recovered;
    }
    catch (const ReauthenticationRequired&)
    {
        if (report_status)
            report_status("Saved authorization expired. Starting quick sign-in.");
        return ReauthenticateWithSavedCredentials(session, report_status, is_cancelled);
    }
}

AuthSession GfnClient::LoginSwitchQR(const LoginProvider& provider, std::function<void(const std::string&)> onUrlGenerated, std::function<bool()> isCancelled) const
{
    AppendAuthLog("auth: QR login started provider=" + provider.code);

    const PkcePair pkce = GeneratePkce();
    const std::string state = HexEncode(GenerateRandomBytes(32).data(), 32);
    
    std::string local_ip = NetworkUtils::GetLocalIPAddress();
    if (local_ip == "127.0.0.1" || local_ip.empty()) {
        throw std::runtime_error("Could not determine local IP address. Are you connected to Wi-Fi?");
    }

    const std::string auth_url = BuildAuthorizeUrl(provider, pkce, state, std::string(kRedirectUri));

    QrCallbackServer callback_server(auth_url);
    callback_server.Start();

    std::string switch_url = "http://" + local_ip + ":" + std::to_string(callback_server.port()) + "/";

    if (onUrlGenerated)
        onUrlGenerated(switch_url);

    const std::string final_url = callback_server.WaitForResult(isCancelled);
    if (final_url.empty())
    {
        throw std::runtime_error("Login was cancelled or timed out.");
    }

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

AuthSession GfnClient::EnsureFreshSession(const AuthSession& session) const
{
    const bool access_needs_refresh = IsNearExpiry(session.tokens.expires_at_ms);
    const bool client_needs_refresh = session.tokens.client_token.empty() ||
        IsNearExpiry(session.tokens.client_token_expires_at_ms);
    const std::int64_t membership_interval = session.user.membership_tier_verified
        ? kMembershipRefreshIntervalMs
        : kMembershipRetryIntervalMs;
    const bool membership_needs_refresh =
        !session.subscription.available ||
        session.membership_checked_at_ms <= 0 ||
        NowMs() - session.membership_checked_at_ms >= membership_interval;

    if (!access_needs_refresh && !client_needs_refresh && !membership_needs_refresh)
        return session;

    AuthSession refreshed = session;

    const bool has_refresh_mechanism =
        (!session.tokens.client_token.empty() && !session.user.user_id.empty()) ||
        !session.tokens.refresh_token.empty();

    if (access_needs_refresh && !has_refresh_mechanism)
    {
        if (IsExpired(session.tokens.expires_at_ms))
            throw ReauthenticationRequired("Saved GeForce NOW login has expired. Please sign in again.");
    }
    else if (access_needs_refresh)
    {
        AppendAuthLog("auth: refreshing access token user_id_present=" +
                      std::string(session.user.user_id.empty() ? "no" : "yes"));
        refreshed.tokens = RefreshSessionTokensNative(session);
        refreshed.last_refresh_at_ms = NowMs();
        AppendAuthLog("auth: access token refresh ok");
    }

    if (refreshed.tokens.client_token.empty() ||
        IsNearExpiry(refreshed.tokens.client_token_expires_at_ms))
    {
        RequestClientTokenNative(refreshed.tokens);
    }
    if (membership_needs_refresh || access_needs_refresh)
        RefreshMembershipTier(http_client_, refreshed);
    return refreshed;
}

AuthSession GfnClient::EnsureFreshSavedSession(const AuthSession& session) const
{
    if (!session.persistence_enabled)
        return EnsureFreshSession(session);

    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    AuthSession source = session;
    for (const AuthSession& saved : LoadAccountsFromDisk(nullptr))
    {
        if (saved.user.user_id == session.user.user_id &&
            saved.provider.idp_id == session.provider.idp_id &&
            saved.provider.streaming_service_url == session.provider.streaming_service_url &&
            (saved.last_refresh_at_ms > source.last_refresh_at_ms ||
             saved.tokens.expires_at_ms > source.tokens.expires_at_ms))
        {
            source = saved;
        }
    }

    AuthSession refreshed = EnsureFreshSession(source);
    const bool changed =
        refreshed.tokens.access_token != source.tokens.access_token ||
        refreshed.tokens.refresh_token != source.tokens.refresh_token ||
        refreshed.tokens.id_token != source.tokens.id_token ||
        refreshed.tokens.client_token != source.tokens.client_token ||
        refreshed.tokens.expires_at_ms != source.tokens.expires_at_ms ||
        refreshed.tokens.client_token_expires_at_ms != source.tokens.client_token_expires_at_ms ||
        refreshed.user.membership_tier != source.user.membership_tier ||
        refreshed.user.membership_tier_verified != source.user.membership_tier_verified ||
        refreshed.membership_checked_at_ms != source.membership_checked_at_ms ||
        refreshed.last_refresh_at_ms != source.last_refresh_at_ms;
    if (changed)
        SaveSession(refreshed);
    return refreshed;
}

AuthSession GfnClient::ForceRefreshSavedSession(const AuthSession& session) const
{
    if (!session.persistence_enabled)
    {
        AuthSession refreshed = session;
        refreshed.tokens = RefreshSessionTokensNative(session);
        if (refreshed.tokens.client_token.empty() ||
            IsNearExpiry(refreshed.tokens.client_token_expires_at_ms))
        {
            RequestClientTokenNative(refreshed.tokens);
        }
        refreshed.last_refresh_at_ms = NowMs();
        RefreshMembershipTier(http_client_, refreshed);
        return refreshed;
    }

    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    AuthSession source = session;
    for (const AuthSession& saved : LoadAccountsFromDisk(nullptr))
    {
        if (saved.user.user_id == session.user.user_id &&
            saved.provider.idp_id == session.provider.idp_id &&
            saved.provider.streaming_service_url == session.provider.streaming_service_url &&
            (saved.last_refresh_at_ms > source.last_refresh_at_ms ||
             saved.tokens.expires_at_ms > source.tokens.expires_at_ms))
        {
            source = saved;
        }
    }

    AppendAuthLog("auth: forced refresh after authorization failure user_id_present=" +
                  std::string(source.user.user_id.empty() ? "no" : "yes"));
    AuthSession refreshed = source;
    refreshed.tokens = RefreshSessionTokensNative(source);
    if (refreshed.tokens.client_token.empty() ||
        IsNearExpiry(refreshed.tokens.client_token_expires_at_ms))
    {
        RequestClientTokenNative(refreshed.tokens);
    }
    refreshed.last_refresh_at_ms = NowMs();
    RefreshMembershipTier(http_client_, refreshed);
    SaveSession(refreshed);
    return refreshed;
}

bool ParseSessionObject(json_t* root, AuthSession& session)
{
    if (!root || !json_is_object(root))
        return false;

    session.provider.idp_id                = GetString(json_object_get(root, "provider"), "idp_id");
    session.provider.code                  = GetString(json_object_get(root, "provider"), "code");
    session.provider.display_name          = GetString(json_object_get(root, "provider"), "display_name");
    session.provider.streaming_service_url =
        EnsureTrailingSlash(GetString(json_object_get(root, "provider"), "streaming_service_url"));
    session.provider.priority = GetInteger(json_object_get(root, "provider"), "priority", 0);

    json_t* tokens = json_object_get(root, "tokens");
    session.tokens.access_token  = GetString(tokens, "access_token");
    session.tokens.refresh_token = GetString(tokens, "refresh_token");
    session.tokens.id_token      = GetString(tokens, "id_token");
    session.tokens.client_token  = GetString(tokens, "client_token");

    json_t* expires = tokens ? json_object_get(tokens, "expires_at_ms") : nullptr;
    if (json_is_integer(expires))
        session.tokens.expires_at_ms = static_cast<std::int64_t>(json_integer_value(expires));

    json_t* client_expires = tokens ? json_object_get(tokens, "client_token_expires_at_ms") : nullptr;
    if (json_is_integer(client_expires))
    {
        session.tokens.client_token_expires_at_ms =
            static_cast<std::int64_t>(json_integer_value(client_expires));
    }

    json_t* user = json_object_get(root, "user");
    session.user.user_id         = GetString(user, "user_id");
    session.user.display_name    = GetString(user, "display_name");
    session.user.email           = GetString(user, "email");
    session.user.avatar_url      = GetString(user, "avatar_url");
    session.user.membership_tier = GetString(user, "membership_tier");
    session.user.membership_tier_verified =
        GetBool(user, "membership_tier_verified", false);

    json_t* subscription = json_object_get(root, "subscription");
    if (json_is_object(subscription))
    {
        session.subscription.membership_tier = GetString(subscription, "membership_tier");
        session.subscription.subscription_type = GetString(subscription, "subscription_type");
        session.subscription.subscription_subtype = GetString(subscription, "subscription_subtype");
        session.subscription.allotted_hours = JsonNumber(subscription, "allotted_hours");
        session.subscription.purchased_hours = JsonNumber(subscription, "purchased_hours");
        session.subscription.rolled_over_hours = JsonNumber(subscription, "rolled_over_hours");
        session.subscription.used_hours = JsonNumber(subscription, "used_hours");
        session.subscription.remaining_hours = JsonNumber(subscription, "remaining_hours");
        session.subscription.total_hours = JsonNumber(subscription, "total_hours");
        session.subscription.is_unlimited = GetBool(subscription, "is_unlimited", false);
        session.subscription.available = GetBool(subscription, "available", false);
        session.subscription.has_storage = GetBool(subscription, "has_storage", false);
        session.subscription.has_storage_usage =
            GetBool(subscription, "has_storage_usage", false);
        session.subscription.storage_size_gb = JsonNumber(subscription, "storage_size_gb");
        session.subscription.storage_used_gb = JsonNumber(subscription, "storage_used_gb");
        session.subscription.storage_region_name =
            GetString(subscription, "storage_region_name");
        session.subscription.storage_region_code =
            GetString(subscription, "storage_region_code");
    }

    json_t* last_refresh = json_object_get(root, "last_refresh_at_ms");
    if (json_is_integer(last_refresh))
        session.last_refresh_at_ms = static_cast<std::int64_t>(json_integer_value(last_refresh));

    json_t* membership_checked = json_object_get(root, "membership_checked_at_ms");
    if (json_is_integer(membership_checked))
    {
        session.membership_checked_at_ms =
            static_cast<std::int64_t>(json_integer_value(membership_checked));
    }

    if (session.provider.idp_id.empty())
        session.provider = DefaultProvider();

    return !session.tokens.access_token.empty() && !session.user.user_id.empty();
}

JsonPtr BuildSessionObject(const AuthSession& session)
{
    JsonPtr root(json_object(), &json_decref);
    JsonPtr provider(json_object(), &json_decref);
    JsonPtr tokens(json_object(), &json_decref);
    JsonPtr user(json_object(), &json_decref);
    JsonPtr subscription(json_object(), &json_decref);

    json_object_set_new(provider.get(), "idp_id", json_string(session.provider.idp_id.c_str()));
    json_object_set_new(provider.get(), "code", json_string(session.provider.code.c_str()));
    json_object_set_new(provider.get(), "display_name", json_string(session.provider.display_name.c_str()));
    json_object_set_new(
        provider.get(),
        "streaming_service_url",
        json_string(session.provider.streaming_service_url.c_str()));
    json_object_set_new(provider.get(), "priority", json_integer(session.provider.priority));

    json_object_set_new(tokens.get(), "access_token", json_string(session.tokens.access_token.c_str()));
    json_object_set_new(tokens.get(), "refresh_token", json_string(session.tokens.refresh_token.c_str()));
    json_object_set_new(tokens.get(), "id_token", json_string(session.tokens.id_token.c_str()));
    json_object_set_new(tokens.get(), "client_token", json_string(session.tokens.client_token.c_str()));
    json_object_set_new(tokens.get(), "expires_at_ms", json_integer(session.tokens.expires_at_ms));
    json_object_set_new(
        tokens.get(),
        "client_token_expires_at_ms",
        json_integer(session.tokens.client_token_expires_at_ms));

    json_object_set_new(user.get(), "user_id", json_string(session.user.user_id.c_str()));
    json_object_set_new(user.get(), "display_name", json_string(session.user.display_name.c_str()));
    json_object_set_new(user.get(), "email", json_string(session.user.email.c_str()));
    json_object_set_new(user.get(), "avatar_url", json_string(session.user.avatar_url.c_str()));
    json_object_set_new(user.get(), "membership_tier", json_string(session.user.membership_tier.c_str()));
    json_object_set_new(
        user.get(), "membership_tier_verified",
        json_boolean(session.user.membership_tier_verified));

    json_object_set_new(
        subscription.get(), "membership_tier",
        json_string(session.subscription.membership_tier.c_str()));
    json_object_set_new(
        subscription.get(), "subscription_type",
        json_string(session.subscription.subscription_type.c_str()));
    json_object_set_new(
        subscription.get(), "subscription_subtype",
        json_string(session.subscription.subscription_subtype.c_str()));
    json_object_set_new(subscription.get(), "allotted_hours", json_real(session.subscription.allotted_hours));
    json_object_set_new(subscription.get(), "purchased_hours", json_real(session.subscription.purchased_hours));
    json_object_set_new(subscription.get(), "rolled_over_hours", json_real(session.subscription.rolled_over_hours));
    json_object_set_new(subscription.get(), "used_hours", json_real(session.subscription.used_hours));
    json_object_set_new(subscription.get(), "remaining_hours", json_real(session.subscription.remaining_hours));
    json_object_set_new(subscription.get(), "total_hours", json_real(session.subscription.total_hours));
    json_object_set_new(subscription.get(), "is_unlimited", json_boolean(session.subscription.is_unlimited));
    json_object_set_new(subscription.get(), "available", json_boolean(session.subscription.available));
    json_object_set_new(subscription.get(), "has_storage", json_boolean(session.subscription.has_storage));
    json_object_set_new(
        subscription.get(), "has_storage_usage",
        json_boolean(session.subscription.has_storage_usage));
    json_object_set_new(subscription.get(), "storage_size_gb", json_real(session.subscription.storage_size_gb));
    json_object_set_new(subscription.get(), "storage_used_gb", json_real(session.subscription.storage_used_gb));
    json_object_set_new(
        subscription.get(), "storage_region_name",
        json_string(session.subscription.storage_region_name.c_str()));
    json_object_set_new(
        subscription.get(), "storage_region_code",
        json_string(session.subscription.storage_region_code.c_str()));

    json_object_set_new(root.get(), "provider", json_incref(provider.get()));
    json_object_set_new(root.get(), "tokens", json_incref(tokens.get()));
    json_object_set_new(root.get(), "user", json_incref(user.get()));
    json_object_set_new(root.get(), "subscription", json_incref(subscription.get()));
    json_object_set_new(root.get(), "last_refresh_at_ms", json_integer(session.last_refresh_at_ms));
    json_object_set_new(
        root.get(), "membership_checked_at_ms",
        json_integer(session.membership_checked_at_ms));
    return root;
}

void WriteJsonToFile(const std::string& path, json_t* root)
{
    char* dump = json_dumps(root, JSON_INDENT(2));
    if (!dump)
        throw std::runtime_error("Failed to serialize JSON");

    std::unique_ptr<char, decltype(&std::free)> output(dump, &std::free);
    WriteTextFileAtomically(path, output.get());
}

void RememberActiveCloudSession(
    const AuthSession& auth,
    const std::string& session_id,
    const std::string& launch_app_id,
    const std::string& streaming_base_url,
    const std::string& device_id,
    const std::string& client_id, gfn::cloud_session::ClientProfile profile)
{
    std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
    if (session_id.empty())
        return;

    try
    {
        JsonPtr root(json_object(), &json_decref);
        json_object_set_new(root.get(), "session_id", json_string(session_id.c_str()));
        gfn::cloud_session::StoreRoutingIdentity(root.get(),
            {auth.user.user_id, auth.provider.idp_id, auth.provider.streaming_service_url, device_id, client_id});
        gfn::cloud_session::StoreClientProfile(root.get(),profile);
        json_object_set_new(root.get(), "launch_app_id", json_string(launch_app_id.c_str()));
        json_object_set_new(
            root.get(),
            "streaming_base_url",
            json_string(streaming_base_url.c_str()));
        json_object_set_new(root.get(), "created_at_ms", json_integer(NowMs()));
        WriteJsonToFile(GetActiveCloudSessionPath(), root.get());
    }
    catch (const std::exception& e)
    {
        AppendAuthLog("session: unable to persist active session error=" + std::string(e.what()));
    }
}

gfn::cloud_session::ClientProfile LoadActiveCloudSessionProfile(
    const AuthSession& auth, const std::string& session_id) {
    std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
    const auto body=ReadTextFile(GetActiveCloudSessionPath());
    if(body.empty()) return gfn::cloud_session::NewClientProfile(auth.provider.code);
    JsonPtr root(nullptr,&json_decref);
    try { root=LoadJson(body); } catch (...) { return gfn::cloud_session::NewClientProfile(auth.provider.code); }
    if(GetString(root.get(),"session_id")!=session_id || GetString(root.get(),"user_id")!=auth.user.user_id ||
        GetString(root.get(),"provider_id")!=auth.provider.idp_id ||
        GetString(root.get(),"provider_url")!=auth.provider.streaming_service_url)
        return gfn::cloud_session::NewClientProfile(auth.provider.code);
    return gfn::cloud_session::ReadClientProfile(root.get());
}

std::string LoadActiveCloudSessionBaseUrl(
    const AuthSession& auth,
    const std::string& session_id)
{
    std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
    const std::string body = ReadTextFile(GetActiveCloudSessionPath());
    if (body.empty())
        return "";

    try
    {
        JsonPtr root = LoadJson(body);
        if (GetString(root.get(), "user_id") != auth.user.user_id ||
            (!session_id.empty() && GetString(root.get(), "session_id") != session_id))
        {
            return "";
        }

        return server_location::NormalizeStreamingBaseUrl(
            GetString(root.get(), "streaming_base_url"));
    }
    catch (...)
    {
        return "";
    }
}

std::string LoadActiveCloudSessionId(const AuthSession& auth)
{
    std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
    const std::string body = ReadTextFile(GetActiveCloudSessionPath());
    if (body.empty())
        return "";

    try
    {
        JsonPtr root = LoadJson(body);
        if (GetString(root.get(), "user_id") != auth.user.user_id)
            return "";
        return GetString(root.get(), "session_id");
    }
    catch (...)
    {
        return "";
    }
}

void ForgetActiveCloudSession(const std::string& session_id)
{
    std::lock_guard<std::recursive_mutex> lock(g_active_cloud_session_mutex);
    const std::string body = ReadTextFile(GetActiveCloudSessionPath());
    if (!body.empty())
    {
        try
        {
            JsonPtr root = LoadJson(body);
            if (!session_id.empty() && GetString(root.get(), "session_id") != session_id)
                return;
        }
        catch (...) {}
    }

    std::remove(GetActiveCloudSessionPath().c_str());
    std::remove((GetActiveCloudSessionPath() + ".bak").c_str());
}

std::vector<AuthSession> LoadAccountsFromDisk(std::string* active_user_id = nullptr)
{
    for (const std::string& path : {GetAccountsPath(), GetAccountsPath() + ".bak"})
    {
        const std::string stored = ReadTextFile(path);
        if (stored.empty())
            continue;

        try
        {
            const std::string body = DecryptTokenVault(stored);
            JsonPtr root = LoadJson(body);
            if (!json_is_object(root.get()))
                continue;

            json_t* accounts = json_object_get(root.get(), "accounts");
            if (!accounts || !json_is_array(accounts))
                continue;

            std::vector<AuthSession> sessions;
            size_t index = 0;
            json_t* item = nullptr;
            json_array_foreach(accounts, index, item)
            {
                AuthSession session;
                if (ParseSessionObject(item, session))
                    sessions.push_back(std::move(session));
            }

            if (sessions.empty())
                continue;

            if (active_user_id)
                *active_user_id = GetString(root.get(), "active_user_id");
            if (path.find(".bak") != std::string::npos)
                AppendAuthLog("auth: recovered accounts from backup");
            else if (stored.rfind(kTokenVaultHeader, 0) != 0)
            {
                AppendAuthLog("auth: loaded legacy plaintext account store; migration scheduled");
                std::string active = GetString(root.get(), "active_user_id");
                if (active.empty())
                    active = sessions.front().user.user_id;
                SaveAccountsToDisk(sessions, active);
                AppendAuthLog("auth: legacy account store migrated to encrypted vault");
            }
            return sessions;
        }
        catch (const std::exception& e)
        {
            AppendAuthLog("auth: account store parse failed path=" + path + " error=" + e.what());
        }
    }

    return {};
}

void SaveAccountsToDisk(const std::vector<AuthSession>& sessions, const std::string& active_user_id)
{
    EnsureAppHome();

    JsonPtr root(json_object(), &json_decref);
    JsonPtr accounts(json_array(), &json_decref);

    for (const AuthSession& session : sessions)
    {
        JsonPtr item = BuildSessionObject(session);
        json_array_append_new(accounts.get(), json_incref(item.get()));
    }

    json_object_set_new(root.get(), "schema_version", json_integer(3));
    json_object_set_new(root.get(), "active_user_id", json_string(active_user_id.c_str()));
    json_object_set_new(root.get(), "accounts", json_incref(accounts.get()));
    char* dump = json_dumps(root.get(), JSON_COMPACT | JSON_SORT_KEYS);
    if (!dump)
        throw std::runtime_error("Failed to serialize token vault");
    std::unique_ptr<char, decltype(&std::free)> plaintext(dump, &std::free);
    const std::string encrypted = EncryptTokenVault(plaintext.get());
    if (DecryptTokenVault(encrypted) != plaintext.get())
        throw std::runtime_error("Token vault round-trip verification failed");
    WriteTextFileAtomically(GetAccountsPath(), encrypted);

    const std::string backup_path = GetAccountsPath() + ".bak";
    const std::string backup = ReadTextFile(backup_path);
    if (!backup.empty() && backup.rfind(kTokenVaultHeader, 0) != 0)
        std::remove(backup_path.c_str());

    // The encrypted multi-account vault replaces the old plaintext session copy.
    std::remove(GetSessionPath().c_str());
    std::remove((GetSessionPath() + ".bak").c_str());
    AppendAuthLog("auth: token vault saved accounts=" + std::to_string(sessions.size()) +
                  " encrypted=1 roundTrip=ok");
}

std::vector<NativeCredentials> LoadNativeCredentialEntries()
{
    const std::string stored = ReadTextFile(GetNativeCredentialsPath());
    if (stored.empty())
        return {};
    try
    {
        JsonPtr root = LoadJson(DecryptTokenVault(stored));
        json_t* entries = json_object_get(root.get(), "entries");
        if (!json_is_array(entries))
            return {};
        std::vector<NativeCredentials> result;
        size_t index = 0;
        json_t* item = nullptr;
        json_array_foreach(entries, index, item)
        {
            NativeCredentials credentials;
            credentials.provider_id = GetString(item, "provider_id");
            credentials.email = GetString(item, "email");
            credentials.password = GetString(item, "password");
            if (!credentials.provider_id.empty() && !credentials.email.empty() &&
                !credentials.password.empty())
            {
                result.push_back(std::move(credentials));
            }
        }
        return result;
    }
    catch (const std::exception& e)
    {
        AppendAuthLog(std::string("auth-native: credential vault load failed error=") + e.what());
        return {};
    }
}

void SaveNativeCredentialEntries(const std::vector<NativeCredentials>& entries)
{
    EnsureAppHome();
    if (entries.empty())
    {
        std::remove(GetNativeCredentialsPath().c_str());
        std::remove((GetNativeCredentialsPath() + ".bak").c_str());
        return;
    }

    JsonPtr root(json_object(), &json_decref);
    JsonPtr values(json_array(), &json_decref);
    for (const NativeCredentials& credentials : entries)
    {
        JsonPtr item(json_object(), &json_decref);
        json_object_set_new(item.get(), "provider_id", json_string(credentials.provider_id.c_str()));
        json_object_set_new(item.get(), "email", json_string(credentials.email.c_str()));
        json_object_set_new(item.get(), "password", json_string(credentials.password.c_str()));
        json_array_append_new(values.get(), json_incref(item.get()));
    }
    json_object_set_new(root.get(), "schema_version", json_integer(1));
    json_object_set_new(root.get(), "entries", json_incref(values.get()));
    char* dump = json_dumps(root.get(), JSON_COMPACT | JSON_SORT_KEYS);
    if (!dump)
        throw std::runtime_error("Failed to serialize the credential vault");
    std::unique_ptr<char, decltype(&std::free)> plaintext(dump, &std::free);
    const std::string encrypted = EncryptTokenVault(plaintext.get());
    if (DecryptTokenVault(encrypted) != plaintext.get())
        throw std::runtime_error("Credential vault round-trip verification failed");
    WriteTextFileAtomically(GetNativeCredentialsPath(), encrypted);
    AppendAuthLog("auth-native: credential vault saved entries=" + std::to_string(entries.size()));
}

bool LoadLegacySession(AuthSession& session)
{
    for (const std::string& path : {GetSessionPath(), GetSessionPath() + ".bak"})
    {
        const std::string body = ReadTextFile(path);
        if (body.empty())
            continue;
        try
        {
            JsonPtr root = LoadJson(body);
            if (ParseSessionObject(root.get(), session))
                return true;
        }
        catch (...) {}
    }

    return false;
}
