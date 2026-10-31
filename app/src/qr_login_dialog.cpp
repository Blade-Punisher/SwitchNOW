#include "qr_login_dialog.hpp"
#include "qrcodegen.h"
#include "app_state.hpp"
#include "avatar_utils.hpp"
#include "cover_image_cache.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <array>
#include <algorithm>
#include <future>

namespace opennow {

QrLoginDialog::QrLoginDialog(const LoginProvider& provider, const GfnClient& client, std::function<void()> on_success)
    : brls::Box(brls::Axis::COLUMN), provider_(provider), client_(client), on_success_(std::move(on_success))
{
    const bool is_nvidia = provider_.code.empty() || provider_.code == "NVIDIA";
    setPadding(24, 46, 24, 46);
    setBackgroundColor(nvgRGB(11, 12, 15));

    auto* header = new brls::Header();
    header->setTitle(Tr("Login with") + " " + provider.display_name);
    header->setMarginBottom(12);
    addView(header);

    status_label_ = new brls::Label();
    status_label_->setText(Tr("Choose how to sign in"));
    status_label_->setFontSize(30);
    status_label_->setTextColor(nvgRGB(244, 247, 250));
    status_label_->setMarginBottom(4);
    addView(status_label_);

    instruction_label_ = new brls::Label();
    instruction_label_->setText(is_nvidia
        ? opennow::Tr("Fast, console-first sign-in. Your saved account is ready without opening the limited Switch browser.")
        : opennow::Tr("This GeForce NOW Alliance account uses the operator's own secure sign-in page. Continue on a phone or PC."));
    instruction_label_->setFontSize(16);
    instruction_label_->setTextColor(nvgRGB(145, 153, 165));
    instruction_label_->setMarginBottom(18);
    addView(instruction_label_);

    if (is_nvidia)
        saved_credentials_ = client_.LoadNativeCredentials(provider_.idp_id);
    remember_password_ = saved_credentials_.has_value();

    AuthUser profile_user;
    if (AppState::Instance().HasSession() &&
        AppState::Instance().session()->provider.idp_id == provider_.idp_id)
        profile_user = AppState::Instance().session()->user;
    if (saved_credentials_)
    {
        for (const AuthSession& session : client_.LoadSavedSessions())
        {
            if (session.user.email == saved_credentials_->email)
            {
                profile_user = session.user;
                break;
            }
        }
        if (profile_user.email.empty())
            profile_user.email = saved_credentials_->email;
    }
    if (profile_user.display_name.empty() && !profile_user.email.empty())
    {
        const auto at = profile_user.email.find('@');
        profile_user.display_name = at == std::string::npos
            ? profile_user.email
            : profile_user.email.substr(0, at);
    }

    auto* body = new brls::Box(brls::Axis::ROW);
    body->setGrow(1.0f);
    body->setAlignItems(brls::AlignItems::STRETCH);
    addView(body);

    auto* profile_card = new brls::Box(brls::Axis::COLUMN);
    profile_card->setWidth(310);
    profile_card->setPadding(24, 24, 24, 24);
    profile_card->setMarginRight(22);
    profile_card->setCornerRadius(16);
    profile_card->setBackgroundColor(nvgRGB(19, 22, 27));
    profile_card->setAlignItems(brls::AlignItems::CENTER);
    profile_card->setJustifyContent(brls::JustifyContent::CENTER);
    body->addView(profile_card);

    auto* avatar_frame = new brls::Box(brls::Axis::COLUMN);
    avatar_frame->setWidth(124);
    avatar_frame->setHeight(124);
    avatar_frame->setPadding(5, 5, 5, 5);
    avatar_frame->setCornerRadius(62);
    avatar_frame->setBackgroundColor(nvgRGB(38, 54, 49));
    avatar_frame->setMarginBottom(16);
    profile_card->addView(avatar_frame);

    auto* avatar = new brls::Image();
    avatar->setGrow(1.0f);
    avatar->setCornerRadius(57);
    avatar->setScalingType(brls::ImageScalingType::FILL);
    avatar_frame->addView(avatar);
    SetCachedAvatarImage(avatar, ResolveAvatarUrl(profile_user));

    auto* profile_name = new brls::Label();
    profile_name->setText(profile_user.display_name.empty()
        ? (is_nvidia ? Tr("NVIDIA account") : provider_.display_name)
        : profile_user.display_name);
    profile_name->setFontSize(23);
    profile_name->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    profile_name->setMarginBottom(5);
    profile_card->addView(profile_name);

    auto* profile_email = new brls::Label();
    profile_email->setText(profile_user.email.empty() ? Tr("No saved account yet") : profile_user.email);
    profile_email->setFontSize(14);
    profile_email->setTextColor(nvgRGB(149, 157, 168));
    profile_email->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    profile_email->setMarginBottom(18);
    profile_card->addView(profile_email);

    auto* ready_badge = new brls::Box(brls::Axis::ROW);
    ready_badge->setHeight(36);
    ready_badge->setPadding(7, 14, 7, 14);
    ready_badge->setCornerRadius(18);
    ready_badge->setBackgroundColor(saved_credentials_ ? nvgRGB(20, 66, 46) : nvgRGB(37, 40, 47));
    ready_badge->setAlignItems(brls::AlignItems::CENTER);
    ready_badge->setJustifyContent(brls::JustifyContent::CENTER);
    auto* ready_label = new brls::Label();
    ready_label->setText(Tr(saved_credentials_
        ? "QUICK SIGN-IN READY"
        : (is_nvidia ? "NEW ACCOUNT" : "ALLIANCE OAUTH")));
    ready_label->setFontSize(13);
    ready_label->setTextColor(saved_credentials_ ? nvgRGB(96, 236, 136) : nvgRGB(175, 181, 191));
    ready_badge->addView(ready_label);
    profile_card->addView(ready_badge);

    auto* actions = new brls::Box(brls::Axis::COLUMN);
    actions->setGrow(1.0f);
    actions->setPadding(18, 22, 18, 22);
    actions->setCornerRadius(16);
    actions->setBackgroundColor(nvgRGB(17, 19, 23));
    body->addView(actions);

    auto* action_title = new brls::Label();
    action_title->setText(Tr(saved_credentials_
        ? "Welcome back"
        : (is_nvidia ? "Connect your account" : "Connect Alliance account")));
    action_title->setFontSize(23);
    action_title->setMarginBottom(5);
    actions->addView(action_title);

    auto* action_hint = new brls::Label();
    action_hint->setText(saved_credentials_
        ? opennow::Tr("Continue with the saved profile, or choose another NVIDIA account.")
        : (is_nvidia
            ? Tr("Sign in directly inside SwitchNOW. Password saving remains optional.")
            : opennow::Tr("SwitchNOW will request OAuth for ") + provider_.display_name +
                opennow::Tr(" and preserve the resulting refresh token.")));
    action_hint->setFontSize(14);
    action_hint->setTextColor(nvgRGB(142, 150, 161));
    action_hint->setMarginBottom(16);
    actions->addView(action_hint);

    device_login_button_ = new brls::Button();
    device_login_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    device_login_button_->setHeight(66);
    device_login_button_->setCornerRadius(12);
    device_login_button_->setFontSize(21);
    device_login_button_->setText(saved_credentials_
        ? opennow::Tr("Quick sign in as ") + saved_credentials_->email
        : (is_nvidia
            ? Tr("Sign in inside SwitchNOW")
            : opennow::Tr("Continue with ") + provider_.display_name));
    device_login_button_->setMarginBottom(10);
    device_login_button_->registerClickAction([this](brls::View* view) {
        return StartDeviceLogin(view);
    });
    actions->addView(device_login_button_);

    if (is_nvidia)
    {
        different_account_button_ = new brls::Button();
        different_account_button_->setHeight(54);
        different_account_button_->setCornerRadius(10);
        different_account_button_->setText(Tr(saved_credentials_ ? "Use another NVIDIA account" : "Enter email and password"));
        different_account_button_->setMarginBottom(9);
        different_account_button_->registerClickAction([this](brls::View* view) {
            return StartDifferentAccountLogin(view);
        });
        actions->addView(different_account_button_);

        qr_login_button_ = new brls::Button();
        qr_login_button_->setStyle(&brls::BUTTONSTYLE_BORDERED);
        qr_login_button_->setHeight(48);
        qr_login_button_->setCornerRadius(10);
        qr_login_button_->setFontSize(16);
        qr_login_button_->setText(Tr("Phone / PC fallback for CAPTCHA or passkey"));
        qr_login_button_->setMarginBottom(13);
        qr_login_button_->registerClickAction([this](brls::View* view) {
            return StartQrLogin(view);
        });
        actions->addView(qr_login_button_);
    }

    auto* preferences_title = new brls::Label();
    preferences_title->setText(Tr("SIGN-IN PREFERENCES"));
    preferences_title->setFontSize(12);
    preferences_title->setTextColor(nvgRGB(105, 113, 125));
    preferences_title->setMarginBottom(7);
    actions->addView(preferences_title);

    auto* preferences = new brls::Box(brls::Axis::ROW);
    preferences->setHeight(48);
    preferences->setMarginBottom(9);
    actions->addView(preferences);

    remember_button_ = new brls::Button();
    remember_button_->setGrow(1.0f);
    remember_button_->setHeight(48);
    remember_button_->setCornerRadius(9);
    remember_button_->setFontSize(15);
    remember_button_->setText(Tr("Remember sign-in: ON"));
    remember_button_->setMarginRight(8);
    remember_button_->registerClickAction([this](brls::View* view) {
        return ToggleRememberLogin(view);
    });
    preferences->addView(remember_button_);

    if (is_nvidia)
    {
        remember_password_button_ = new brls::Button();
        remember_password_button_->setGrow(1.0f);
        remember_password_button_->setHeight(48);
        remember_password_button_->setCornerRadius(9);
        remember_password_button_->setFontSize(15);
        remember_password_button_->setText(
            std::string(opennow::Tr("Save password: ")) + (remember_password_ ? opennow::Tr("ON") : opennow::Tr("OFF")));
        remember_password_button_->registerClickAction([this](brls::View* view) {
            return ToggleRememberPassword(view);
        });
        preferences->addView(remember_password_button_);
    }

    cancel_login_button_ = new brls::Button();
    cancel_login_button_->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
    cancel_login_button_->setHeight(38);
    cancel_login_button_->setFontSize(14);
    cancel_login_button_->setTextColor(nvgRGB(184, 190, 200));
    cancel_login_button_->setText(Tr("Cancel active sign-in"));
    cancel_login_button_->registerClickAction([this](brls::View* view) {
        return CancelActiveLogin(view);
    });
    actions->addView(cancel_login_button_);

    url_label_ = new brls::Label();
    url_label_->setText("");
    url_label_->setFontSize(12);
    url_label_->setTextColor(nvgRGB(96, 236, 136));
    url_label_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    url_label_->setMarginTop(4);
    actions->addView(url_label_);
}

QrLoginDialog::~QrLoginDialog()
{
    lifetime_guard_->store(false);
    is_cancelled_ = true;
    if (worker_.joinable())
        worker_.join();
}

void QrLoginDialog::willDisappear(bool resetState)
{
    brls::Box::willDisappear(resetState);
    is_cancelled_ = true;
}

void QrLoginDialog::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    if (device_login_button_ && !login_active_.load())
        brls::Application::giveFocus(device_login_button_);
}

bool QrLoginDialog::ToggleRememberLogin(brls::View* view)
{
    (void)view;
    if (login_active_)
        return true;
    remember_login_ = !remember_login_;
    remember_button_->setText(std::string(opennow::Tr("Remember sign-in: ")) + (remember_login_ ? opennow::Tr("ON") : opennow::Tr("OFF")));
    instruction_label_->setText(remember_login_
        ? opennow::Tr("OAuth tokens will be saved and renewed automatically. Password storage is controlled separately below.")
        : opennow::Tr("This account will be used until SwitchNOW closes and will not be saved."));
    return true;
}

bool QrLoginDialog::ToggleRememberPassword(brls::View* view)
{
    (void)view;
    if (login_active_)
        return true;
    remember_password_ = !remember_password_;
    remember_password_button_->setText(
        std::string(opennow::Tr("Save password: ")) + (remember_password_ ? opennow::Tr("ON") : opennow::Tr("OFF")));
    instruction_label_->setText(remember_password_
        ? opennow::Tr("The password will be stored in the encrypted SwitchNOW vault for one-button sign-in.")
        : opennow::Tr("The password will not be stored. Saved credentials are removed after the next successful sign-in."));
    return true;
}

bool QrLoginDialog::CancelActiveLogin(brls::View* view)
{
    (void)view;
    if (!login_active_)
        return true;
    is_cancelled_ = true;
    status_label_->setText(Tr("Cancelling sign-in..."));
    instruction_label_->setText(Tr("The current NVIDIA request will stop safely."));
    return true;
}

void QrLoginDialog::ShowEmailWaitingDialog(const std::string& status)
{
    if (email_waiting_dialog_)
    {
        if (email_waiting_label_)
            email_waiting_label_->setText(TrMessage(status));
        return;
    }

    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setPadding(28, 36, 28, 36);
    content->setAlignItems(brls::AlignItems::CENTER);

    auto* title = new brls::Label();
    title->setText(Tr("Approve the NVIDIA sign-in by email"));
    title->setFontSize(28);
    title->setMarginBottom(20);
    content->addView(title);

    auto* spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::LARGE);
    spinner->setMarginBottom(20);
    content->addView(spinner);

    email_waiting_label_ = new brls::Label();
    email_waiting_label_->setText(TrMessage(status));
    email_waiting_label_->setFontSize(20);
    email_waiting_label_->setMarginBottom(12);
    content->addView(email_waiting_label_);

    auto* hint = new brls::Label();
    hint->setText(Tr("Open the message from NVIDIA on your phone or PC and confirm the login. SwitchNOW will continue automatically."));
    hint->setFontSize(18);
    hint->setTextColor(nvgRGB(118, 185, 0));
    content->addView(hint);

    auto* dialog = new brls::Dialog(content);
    email_waiting_dialog_ = dialog;
    dialog->setCancelable(false);
    dialog->addButton(Tr("Cancel sign-in"), [this]() {
        is_cancelled_ = true;
        email_waiting_dialog_ = nullptr;
        email_waiting_label_ = nullptr;
        status_label_->setText(Tr("Cancelling sign-in..."));
        instruction_label_->setText(opennow::Tr("The NVIDIA email approval request is being cancelled."));
    });
    dialog->open();
}

void QrLoginDialog::CloseEmailWaitingDialog(std::function<void()> after_close)
{
    brls::Dialog* dialog = email_waiting_dialog_;
    email_waiting_dialog_ = nullptr;
    email_waiting_label_ = nullptr;
    if (dialog)
        dialog->close(std::move(after_close));
    else if (after_close)
        after_close();
}

void QrLoginDialog::CompleteLogin(const AuthSession& session)
{
    AuthSession active_session = session;
    active_session.persistence_enabled = remember_login_;
    if (remember_login_)
        client_.SaveSession(active_session);
    AppState::Instance().SetSession(active_session);
    brls::Application::notify(
        std::string(remember_login_ ? opennow::Tr("Account saved: ") : opennow::Tr("Signed in for this launch: ")) +
        session.user.display_name);
    brls::Application::popActivity();
    if (on_success_)
        on_success_();
}

bool QrLoginDialog::StartDeviceLogin(brls::View* view)
{
    if (!provider_.code.empty() && provider_.code != "NVIDIA")
        return StartQrLogin(view);
    return StartNativeLogin(false);
}

bool QrLoginDialog::StartDifferentAccountLogin(brls::View* view)
{
    if (!provider_.code.empty() && provider_.code != "NVIDIA")
        return StartQrLogin(view);
    return StartNativeLogin(true);
}

std::string QrLoginDialog::PromptText(
    const std::string& title,
    const std::string& initial,
    bool password,
    bool numeric)
{
#ifdef __SWITCH__
    const std::uint64_t operation_id = BeginRuntimeOperation(
        "auth_ui", "keyboard_prompt",
        "kind=" + std::string(password ? "password" : (numeric ? "verification" : "email")));
    SwkbdConfig keyboard {};
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
    {
        EndRuntimeOperation(
            operation_id, "auth_ui", "keyboard_prompt", "error",
            "swkbdCreate_failed");
        return {};
    }
    if (password)
        swkbdConfigMakePresetPassword(&keyboard);
    else
        swkbdConfigMakePresetDefault(&keyboard);
    if (numeric)
    {
        swkbdConfigSetType(&keyboard, SwkbdType_NumPad);
        swkbdConfigSetStringLenMin(&keyboard, 6);
        swkbdConfigSetStringLenMax(&keyboard, 12);
    }
    else
    {
        swkbdConfigSetStringLenMax(&keyboard, password ? 128 : 254);
    }
    swkbdConfigSetHeaderText(&keyboard, TrMessage(title).c_str());
    swkbdConfigSetOkButtonText(&keyboard, Tr("Continue").c_str());
    if (!initial.empty() && !password)
        swkbdConfigSetInitialText(&keyboard, initial.c_str());
    std::array<char, 512> output {};
    const Result rc = swkbdShow(&keyboard, output.data(), output.size());
    swkbdClose(&keyboard);
    if (R_FAILED(rc))
    {
        EndRuntimeOperation(
            operation_id, "auth_ui", "keyboard_prompt", "cancelled",
            "rc=" + std::to_string(rc));
        return {};
    }
    EndRuntimeOperation(
        operation_id, "auth_ui", "keyboard_prompt", "ok",
        "characters=" + std::to_string(std::char_traits<char>::length(output.data())));
    return output.data();
#else
    (void)title;
    (void)initial;
    (void)password;
    (void)numeric;
    return {};
#endif
}

bool QrLoginDialog::StartNativeLogin(bool force_prompt)
{
    if (!provider_.code.empty() && provider_.code != "NVIDIA")
        return StartQrLogin(nullptr);

    if (login_active_.exchange(true))
        return true;
    LogRuntimeEvent(
        "auth_ui", "native_login_start",
        "provider=" + provider_.code +
        " force_prompt=" + std::to_string(force_prompt ? 1 : 0));
    is_cancelled_ = false;

    status_label_->setText(Tr("Signing in directly with NVIDIA..."));
    instruction_label_->setText(Tr("SwitchNOW is using NVIDIA's official JSON login flow. No browser applet is opened."));
    url_label_->setText("");
    qr_size_ = 0;

    NativeCredentials credentials;
    credentials.provider_id = provider_.idp_id;
    if (!force_prompt && saved_credentials_)
    {
        credentials = *saved_credentials_;
    }
    else
    {
        std::string initial_email;
        if (saved_credentials_)
            initial_email = saved_credentials_->email;
        else if (AppState::Instance().HasSession())
            initial_email = AppState::Instance().session()->user.email;
        credentials.email = PromptText(opennow::Tr("NVIDIA account email"), initial_email, false);
        if (credentials.email.empty())
        {
            LogRuntimeEvent("auth_ui", "native_login_cancelled", "stage=email");
            status_label_->setText(Tr("Sign-in cancelled"));
            login_active_.store(false);
            return true;
        }
        credentials.password = PromptText(opennow::Tr("NVIDIA account password"), {}, true);
        if (credentials.password.empty())
        {
            LogRuntimeEvent("auth_ui", "native_login_cancelled", "stage=password");
            status_label_->setText(Tr("Sign-in cancelled"));
            login_active_.store(false);
            return true;
        }
    }

    if (worker_.joinable())
        worker_.join();

    const bool save_password = remember_password_;
    const auto guard = lifetime_guard_;
    worker_ = std::thread([this, guard, credentials = std::move(credentials), save_password]() mutable {
        try
        {
            const AuthSession session = client_.LoginNative(
                provider_, credentials.email, credentials.password,
                [this, guard](const std::string& prompt) {
                    auto result = std::make_shared<std::promise<std::string>>();
                    std::future<std::string> future = result->get_future();
                    brls::sync([this, guard, result, prompt]() {
                        if (!guard->load() || is_cancelled_.load())
                        {
                            result->set_value({});
                            return;
                        }
                        result->set_value(QrLoginDialog::PromptText(prompt, {}, false, true));
                    });
                    while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
                    {
                        if (is_cancelled_ || !guard->load())
                            return std::string {};
                    }
                    return future.get();
                },
                [this, guard](const std::string& status) {
                    brls::sync([this, guard, status]() {
                        if (!guard->load())
                            return;
                        status_label_->setText(Tr("Waiting for NVIDIA verification"));
                        instruction_label_->setText(TrMessage(status) + opennow::Tr(" Press B or Cancel to stop."));
                        ShowEmailWaitingDialog(status);
                    });
                },
                [this, guard]() {
                    return is_cancelled_.load() || !guard->load();
                });
            LogRuntimeEvent("auth_ui", "native_login_success");

            NativeCredentials saved_copy;
            if (save_password)
            {
                client_.SaveNativeCredentials(credentials);
                saved_copy = credentials;
            }
            else
            {
                client_.ClearNativeCredentials(provider_.idp_id);
            }
            std::fill(credentials.password.begin(), credentials.password.end(), '\0');

            brls::sync([this, guard, session, saved_copy = std::move(saved_copy), save_password]() mutable {
                if (!guard->load())
                    return;
                if (save_password)
                    saved_credentials_ = std::move(saved_copy);
                else
                    saved_credentials_.reset();
                CloseEmailWaitingDialog([this, guard, session]() {
                    if (guard->load())
                        CompleteLogin(session);
                });
            });
        }
        catch (const NativeLoginFallbackRequired& e)
        {
            std::fill(credentials.password.begin(), credentials.password.end(), '\0');
            const std::string message = e.what();
            LogRuntimeEvent("auth_ui", "native_login_fallback", message);
            brls::sync([this, guard, message]() {
                if (!guard->load())
                    return;
                CloseEmailWaitingDialog();
                status_label_->setText(Tr("NVIDIA requires an external verification step"));
                instruction_label_->setText(message +
                    opennow::Tr(". Use the phone / PC fallback below; the limited Switch browser is not required."));
                login_active_.store(false);
            });
        }
        catch (const std::exception& e)
        {
            std::fill(credentials.password.begin(), credentials.password.end(), '\0');
            const std::string message = e.what();
            LogRuntimeEvent("auth_ui", "native_login_error", message);
            brls::sync([this, guard, message]() {
                if (!guard->load())
                    return;
                CloseEmailWaitingDialog();
                status_label_->setText(message.find("cancelled") != std::string::npos
                    ? opennow::Tr("Sign-in cancelled")
                    : opennow::Tr("Native NVIDIA login failed"));
                instruction_label_->setText(message.find("cancelled") != std::string::npos
                    ? opennow::Tr("You can start the internal sign-in again when ready.")
                    : message + opennow::Tr(". Retry, enter another account, or use the phone / PC fallback."));
                login_active_.store(false);
            });
        }
    });
    return true;
}

bool QrLoginDialog::StartQrLogin(brls::View* view)
{
    (void)view;
    if (login_active_.exchange(true))
        return true;
    if (worker_.joinable())
        worker_.join();
    StartQrLoginProcess();
    return true;
}

void QrLoginDialog::StartQrLoginProcess()
{
    status_label_->setText(
        provider_.code == "NVIDIA"
            ? Tr("Starting QR fallback...")
            : Tr("Starting Alliance login..."));
    worker_ = std::thread([this]() {
        try {
            auto onUrlGenerated = [this](const std::string& url) {
                auth_url_ = url;
                GenerateQrCode(url);

                brls::sync([this]() {
                    status_label_->setText(Tr("Scan the QR code with your phone or PC"));
                    instruction_label_->setText(
                        opennow::Tr("Open the Switch page, sign in to ") + provider_.display_name +
                        opennow::Tr(", then paste the final localhost:2259 URL into that page. "
                        "You will not need to repeat this while the saved refresh token remains valid."));
                    url_label_->setText(auth_url_);
                });
            };

            auto isCancelled = [this]() {
                return is_cancelled_.load();
            };

            AuthSession session = client_.LoginSwitchQR(provider_, onUrlGenerated, isCancelled);

            if (is_cancelled_)
                return;

            brls::sync([this, session]() {
                CompleteLogin(session);
            });

        } catch (const std::exception& e) {
            if (is_cancelled_)
                return;

            std::string err_msg = e.what();
            brls::sync([this, err_msg]() {
                status_label_->setText(Tr("Login failed"));
                instruction_label_->setText(err_msg);
                url_label_->setText("");
                qr_size_ = 0;
                login_active_.store(false);
            });
        }
    });
}

void QrLoginDialog::GenerateQrCode(const std::string& url)
{
    std::vector<uint8_t> temp_buffer(qrcodegen_BUFFER_LEN_MAX);
    qr_data_.resize(qrcodegen_BUFFER_LEN_MAX);

    bool success = qrcodegen_encodeText(
        url.c_str(),
        temp_buffer.data(),
        qr_data_.data(),
        qrcodegen_Ecc_LOW,
        qrcodegen_VERSION_MIN,
        qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO,
        true);

    if (success) {
        qr_size_ = qrcodegen_getSize(qr_data_.data());
    } else {
        qr_size_ = 0;
    }
}

void QrLoginDialog::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    brls::Box::draw(vg, x, y, width, height, style, ctx);

    if (qr_size_ > 0 && !qr_data_.empty()) {
        float cell_size = 6.0f; // 6 pixels per QR module
        float qr_px_size = qr_size_ * cell_size;
        
        float start_x = x + (width - qr_px_size) / 2.0f;
        float start_y = y + (height - qr_px_size) / 2.0f + 82.0f;

        // Draw white background with padding
        nvgBeginPath(vg);
        nvgRect(vg, start_x - 12, start_y - 12, qr_px_size + 24, qr_px_size + 24);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);

        // Draw QR modules
        nvgBeginPath(vg);
        for (int row = 0; row < qr_size_; row++) {
            for (int col = 0; col < qr_size_; col++) {
                if (qrcodegen_getModule(qr_data_.data(), col, row)) {
                    nvgRect(vg, start_x + col * cell_size, start_y + row * cell_size, cell_size, cell_size);
                }
            }
        }
        nvgFillColor(vg, nvgRGB(0, 0, 0));
        nvgFill(vg);
    }
}

} // namespace opennow
