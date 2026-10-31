#include "settings_tab.hpp"

#include "app_state.hpp"
#include "app_paths.hpp"
#include "app_version.hpp"
#include "localization.hpp"
#include "membership_tier_policy.hpp"
#include "membership_tier_style.hpp"
#include "network_utils.hpp"
#include "runtime_journal.hpp"
#include "server_location_policy.hpp"
#include "server_zone_policy.hpp"
#include "stream_settings.hpp"
#include "proxy_diagnostics.hpp"
#include "stream_settings_policy.hpp"
#include "stream_diagnostics.hpp"
#include "subscription_display.hpp"
#include "update_service.hpp"
#include "ui_action_guard.hpp"
#include "ui_helpers.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <cstdint>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace opennow
{
namespace
{

brls::Label* MakeParagraph(const std::string& text, float bottom_margin = 16.0f)
{
    auto* label = new brls::Label();
    label->setText(Tr(text));
    label->setFontSize(18);
    label->setMarginBottom(bottom_margin);
    return label;
}

const std::string kImageCachePath = AppHomePath() + "/cache/images";
const std::string kAppHomePath = AppHomePath();

struct ImageCacheInfo
{
    std::uint64_t bytes = 0;
    size_t files        = 0;
};

std::string FormatBytes(std::uint64_t bytes)
{
    if (bytes < 1024)
        return std::to_string(bytes) + " B";

    const double kib = static_cast<double>(bytes) / 1024.0;
    if (kib < 1024.0)
        return std::to_string(static_cast<int>(kib)) + " KiB";

    const double mib = kib / 1024.0;
    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(1) << mib << " MiB";
    return formatted.str();
}

std::string FormatTokenState(const AuthSession& session)
{
    if (session.reauthentication_required)
        return Tr("reconnect required");

    if (!session.tokens.refresh_token.empty())
        return Tr("connected; automatic renewal enabled");

    if (session.tokens.expires_at_ms <= 0)
        return Tr("connected for this launch");

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto remaining_minutes = (session.tokens.expires_at_ms - now_ms) / 60000;
    if (remaining_minutes <= 0)
        return Tr("expired; reconnect required");
    if (remaining_minutes < 60)
        return TrFormat("temporary login valid for {0} min",{std::to_string(remaining_minutes)});
    return TrFormat("temporary login valid for {0} h",{std::to_string(remaining_minutes/60)});
}

ImageCacheInfo ReadImageCacheInfo()
{
    ImageCacheInfo info;

    DIR* dir = opendir(kImageCachePath.c_str());
    if (!dir)
        return info;

    while (dirent* entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name.empty() || name == "." || name == "..")
            continue;

        const std::string path = kImageCachePath + "/" + name;
        struct stat st {};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        ++info.files;
        info.bytes += static_cast<std::uint64_t>(st.st_size);
    }

    closedir(dir);
    return info;
}

size_t ClearImageCacheFiles()
{
    size_t removed = 0;

    DIR* dir = opendir(kImageCachePath.c_str());
    if (!dir)
        return removed;

    while (dirent* entry = readdir(dir))
    {
        const std::string name = entry->d_name;
        if (name.empty() || name == "." || name == "..")
            continue;

        const std::string path = kImageCachePath + "/" + name;
        struct stat st {};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        if (std::remove(path.c_str()) == 0)
            ++removed;
    }

    closedir(dir);
    return removed;
}

std::string AppletTypeName()
{
#ifdef __SWITCH__
    switch (appletGetAppletType())
    {
        case AppletType_Application:
            return "Application";
        case AppletType_SystemApplication:
            return "SystemApplication";
        case AppletType_LibraryApplet:
            return "LibraryApplet";
        case AppletType_OverlayApplet:
            return "OverlayApplet";
        case AppletType_SystemApplet:
            return "SystemApplet";
        default:
            return "Unknown";
    }
#else
    return "Non-Switch build";
#endif
}

std::string FileState(const std::string& path)
{
    struct stat st {};
    if (stat(path.c_str(), &st) != 0)
        return Tr("Unavailable");

    return std::to_string(static_cast<long long>(st.st_size)) + Tr(" bytes");
}

bool SameSettings(const StreamSettings& left, const StreamSettings& right)
{
    return left.preset_id == right.preset_id && left.label == right.label &&
           left.width == right.width && left.height == right.height &&
           left.fps == right.fps && left.bitrate_kbps == right.bitrate_kbps &&
           left.codec == right.codec && left.region == right.region &&
           left.audio_enabled == right.audio_enabled &&
           left.audio_volume == right.audio_volume &&
           left.audio_buffer_ms == right.audio_buffer_ms &&
           left.video_backend == right.video_backend &&
           left.debug_diagnostics == right.debug_diagnostics &&
           left.stats_overlay_enabled == right.stats_overlay_enabled &&
           left.proxy_enabled == right.proxy_enabled &&
           left.proxy_url == right.proxy_url &&
           left.proxy_username == right.proxy_username &&
           left.proxy_password == right.proxy_password &&
           left.game_language == right.game_language &&
           left.persist_game_settings == right.persist_game_settings &&
           left.controller_layout == right.controller_layout &&
           left.image_quality_mode == right.image_quality_mode &&
           left.interface_language == right.interface_language;
}

} // namespace

SettingsTab::SettingsTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(18, 28, 20, 28);

    auto* top = new brls::Box(brls::Axis::ROW);
    top->setHeight(72);
    top->setAlignItems(brls::AlignItems::CENTER);
    top->setMarginBottom(12);

    auto* title_column = new brls::Box(brls::Axis::COLUMN);
    title_column->setGrow(1.0f);
    auto* title = MakeParagraph(opennow::Tr("Settings"), 3.0f);
    title_label_=title;
    title->setFontSize(30);
    title_column->addView(title);
    auto* hint = MakeParagraph(opennow::Tr("Console-first controls. Press X to save, Y to revert."), 0.0f);
    hint_label_=hint;
    hint->setFontSize(15);
    hint->setTextColor(nvgRGB(142, 149, 160));
    title_column->addView(hint);
    top->addView(title_column);

    save_status_ = MakeParagraph(opennow::Tr("All changes saved"), 0.0f);
    save_status_->setFontSize(16);
    save_status_->setTextColor(nvgRGB(88, 230, 146));
    top->addView(save_status_);
    addView(top);

    auto* body = new brls::Box(brls::Axis::ROW);
    body->setGrow(1.0f);

    auto* sidebar = new brls::Box(brls::Axis::COLUMN);
    sidebar->setWidth(238);
    sidebar->setPadding(16, 14, 16, 14);
    sidebar->setMarginRight(18);
    sidebar->setCornerRadius(14);
    sidebar->setBackgroundColor(nvgRGB(15, 17, 21));

    auto* nav_label = MakeParagraph(opennow::Tr("CATEGORIES"), 12.0f);
    nav_label_=nav_label;
    nav_label->setFontSize(13);
    nav_label->setTextColor(nvgRGB(108, 115, 126));
    sidebar->addView(nav_label);

    const std::array<std::pair<const char*, Category>, 7> categories {{
        {"Account", Category::Account},
        {"Stream", Category::Stream},
        {"Game", Category::Game},
        {"Controls", Category::Controls},
        {"Audio", Category::Audio},
        {"Storage", Category::Storage},
        {"Interface", Category::Interface},
    }};
    for (const auto& [label, category] : categories)
    {
        auto* button = new brls::Button();
        button->setText(Tr(label));
        button->setHeight(58);
        button->setMarginBottom(10);
        button->setCornerRadius(10);
        button->registerClickAction([this, category](brls::View*) {
            SelectCategory(category);
            return true;
        });
        category_buttons_.push_back(button);
        sidebar->addView(button);
    }

    auto* nav_hint = MakeParagraph(
        opennow::Tr("Changes apply to the next stream. Account and cache actions run immediately."), 0.0f);
    nav_hint_=nav_hint;
    nav_hint->setFontSize(14);
    nav_hint->setTextColor(nvgRGB(124, 132, 144));
    nav_hint->setSingleLine(false);
    sidebar->addView(nav_hint);
    body->addView(sidebar);

    auto* content_shell = new brls::Box(brls::Axis::COLUMN);
    content_shell->setGrow(1.0f);
    content_shell->setPadding(0, 6, 0, 6);

    page_title_ = MakeParagraph(opennow::Tr("Stream"), 2.0f);
    page_title_->setFontSize(27);
    content_shell->addView(page_title_);
    page_subtitle_ = MakeParagraph(opennow::Tr("Tune quality, latency and decoder behavior."), 12.0f);
    page_subtitle_->setFontSize(15);
    page_subtitle_->setTextColor(nvgRGB(142, 149, 160));
    content_shell->addView(page_subtitle_);

    scrolling_frame_ = new brls::ScrollingFrame();
    scrolling_frame_->setGrow(1.0f);
    scrolling_frame_->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    content_container_ = new brls::Box(brls::Axis::COLUMN);
    content_container_->setPadding(0, 0, 28, 0);
    scrolling_frame_->setContentView(content_container_);
    content_shell->addView(scrolling_frame_);
    body->addView(content_shell);
    addView(body);

    registerAction(opennow::Tr("Save"), brls::BUTTON_X, [this](brls::View* view) {
        return RunUiAction("settings_save", [this, view] {
            SaveChanges(view);
        });
    }, false, true);
    registerAction(opennow::Tr("Revert"), brls::BUTTON_Y, [this](brls::View* view) {
        return RunUiAction("settings_revert", [this, view] {
            RevertChanges(view);
        });
    }, false, true);

    SelectCategory(Category::Stream);
}

SettingsTab::~SettingsTab()
{
    alive_->store(false, std::memory_order_release);
}

brls::Box* SettingsTab::MakeSection(const std::string& title, const std::string& subtitle)
{
    auto* section = new brls::Box(brls::Axis::COLUMN);
    section->setPadding(18, 18, 18, 18);
    section->setMarginBottom(14);
    section->setCornerRadius(13);
    section->setBorderThickness(1);
    section->setBorderColor(nvgRGB(42, 46, 54));
    section->setBackgroundColor(nvgRGB(20, 22, 27));

    auto* label = MakeParagraph(title, subtitle.empty() ? 14.0f : 4.0f);
    label->setFontSize(21);
    label->setTextColor(nvgRGB(242, 244, 247));
    section->addView(label);
    if (!subtitle.empty())
    {
        auto* detail = MakeParagraph(subtitle, 14.0f);
        detail->setFontSize(14);
        detail->setTextColor(nvgRGB(133, 141, 152));
        section->addView(detail);
    }
    return section;
}

brls::Box* SettingsTab::MakeOptionRow(
    const std::string& title,
    const std::string& description,
    std::function<std::string()> value,
    std::function<bool(brls::View*)> action)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setHeight(90);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(8, 12, 8, 12);
    row->setMarginBottom(8);
    row->setCornerRadius(10);
    row->setBackgroundColor(nvgRGB(25, 28, 34));

    auto* copy = new brls::Box(brls::Axis::COLUMN);
    copy->setGrow(1.0f);
    auto* name = MakeParagraph(title, 2.0f);
    name->setFontSize(18);
    copy->addView(name);
    auto* detail = MakeParagraph(description, 0.0f);
    detail->setFontSize(13);
    detail->setTextColor(nvgRGB(132, 140, 151));
    copy->addView(detail);
    row->addView(copy);

    auto* button = new brls::Button();
    button->setWidth(250);
    button->setHeight(50);
    button->setFontSize(16);
    button->setCornerRadius(9);
    button->setText(Tr(value()));
    button->registerClickAction([this, action = std::move(action)](brls::View* view) {
        const bool handled = action ? action(view) : true;
        UpdateOptionValues();
        return handled;
    });
    option_values_.push_back({button, std::move(value)});
    row->addView(button);
    return row;
}

brls::Box* SettingsTab::MakeActionRow(
    const std::string& title,
    const std::string& description,
    const std::string& button_text,
    std::function<bool(brls::View*)> action,
    bool destructive)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setHeight(90);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setPadding(8, 12, 8, 12);
    row->setMarginBottom(8);
    row->setCornerRadius(10);
    row->setBackgroundColor(destructive ? nvgRGB(42, 24, 27) : nvgRGB(25, 28, 34));

    auto* copy = new brls::Box(brls::Axis::COLUMN);
    copy->setGrow(1.0f);
    auto* name = MakeParagraph(title, 2.0f);
    name->setFontSize(18);
    name->setTextColor(destructive ? nvgRGB(255, 170, 174) : nvgRGB(238, 241, 245));
    copy->addView(name);
    auto* detail = MakeParagraph(description, 0.0f);
    detail->setFontSize(13);
    detail->setTextColor(nvgRGB(132, 140, 151));
    copy->addView(detail);
    row->addView(copy);

    auto* button = new brls::Button();
    button->setWidth(250);
    button->setHeight(50);
    button->setFontSize(16);
    button->setCornerRadius(9);
    button->setText(Tr(button_text));
    button->registerClickAction(std::move(action));
    row->addView(button);
    return row;
}

brls::Label* SettingsTab::AddInfoLine(
    brls::Box* parent,
    const std::string& label,
    const std::string& value)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setHeight(40);
    row->setPadding(5, 8, 5, 8);
    row->setMarginBottom(2);
    row->setCornerRadius(7);
    row->setHighlightCornerRadius(7);
    row->setHighlightPadding(0);
    row->setFocusable(true);
    auto* key = MakeParagraph(label, 0.0f);
    key->setGrow(1.0f);
    key->setFontSize(15);
    key->setTextColor(nvgRGB(139, 147, 158));
    row->addView(key);
    auto* text = MakeParagraph(TrMessage(value), 0.0f);
    text->setFontSize(15);
    text->setTextColor(nvgRGB(210, 216, 225));
    row->addView(text);
    parent->addView(row);
    return text;
}

void SettingsTab::SelectCategory(Category category)
{
    category_ = category;
    switch (category_)
    {
        case Category::Account:
            page_title_->setText(Tr("Account"));
            page_subtitle_->setText(Tr("Manage your GeForce NOW identity and persistent sign-in."));
            break;
        case Category::Stream:
            page_title_->setText(Tr("Stream"));
            page_subtitle_->setText(Tr("Balance image quality, latency and decoder stability."));
            break;
        case Category::Game:
            page_title_->setText(Tr("Game"));
            page_subtitle_->setText(Tr("Choose in-game localization and NVIDIA graphics persistence."));
            break;
        case Category::Controls:
            page_title_->setText(Tr("Controls"));
            page_subtitle_->setText(Tr("Choose whether face buttons follow Xbox positions or Switch labels."));
            break;
        case Category::Audio:
            page_title_->setText(Tr("Audio"));
            page_subtitle_->setText(Tr("Tune stream sound without changing the console volume."));
            break;
        case Category::Storage:
            page_title_->setText(Tr("Storage & system"));
            page_subtitle_->setText(Tr("Inspect cache usage, diagnostics and local app data."));
            break;
        case Category::Interface:
            page_title_->setText(Tr("Interface"));
            page_subtitle_->setText(Tr("Choose the launcher language."));
            break;
    }

    UpdateCategoryChrome();
    RebuildCategory();
}

void SettingsTab::UpdateCategoryChrome()
{
    const size_t selected = static_cast<size_t>(category_);
    for (size_t index = 0; index < category_buttons_.size(); ++index)
    {
        auto* button = category_buttons_[index];
        const bool active = index == selected;
        button->setBackgroundColor(active ? nvgRGB(22, 55, 42) : nvgRGB(24, 27, 32));
        button->setBorderThickness(active ? 2.0f : 0.0f);
        button->setBorderColor(active ? nvgRGB(74, 225, 142) : nvgRGB(24, 27, 32));
    }
}

void SettingsTab::RebuildCategory()
{
    if (!content_container_)
        return;

    const size_t selected = static_cast<size_t>(category_);
    brls::View* fallback = selected < category_buttons_.size()
        ? static_cast<brls::View*>(category_buttons_[selected])
        : static_cast<brls::View*>(this);
    MoveFocusBeforeDestroy(content_container_, fallback);
    option_values_.clear();
    content_container_->clearViews();
    switch (category_)
    {
        case Category::Account:
            BuildAccountPage();
            break;
        case Category::Stream:
            BuildStreamPage();
            break;
        case Category::Game:
            BuildGamePage();
            break;
        case Category::Controls:
            BuildControlsPage();
            break;
        case Category::Audio:
            BuildAudioPage();
            break;
        case Category::Storage:
            BuildStoragePage();
            break;
        case Category::Interface:
            BuildInterfacePage();
            break;
    }
    if (scrolling_frame_)
        scrolling_frame_->setContentOffsetY(0.0f, false);
    UpdateOptionValues();
}

void SettingsTab::ScheduleCategoryRebuild()
{
    if (rebuild_scheduled_)
        return;

    rebuild_scheduled_ = true;
    const auto alive = alive_;
    brls::sync([this, alive] {
        if (!alive->load(std::memory_order_acquire))
            return;

        rebuild_scheduled_ = false;
        RebuildCategory();
        const size_t selected = static_cast<size_t>(category_);
        if (selected < category_buttons_.size())
            brls::Application::giveFocus(category_buttons_[selected]);
    });
}

void SettingsTab::BuildAccountPage()
{
    const auto& state = AppState::Instance();
    auto* overview = MakeSection(opennow::Tr("Connected account"), opennow::Tr("Authentication is renewed automatically when NVIDIA permits it."));
    if (!state.HasSession())
    {
        AddInfoLine(overview, opennow::Tr("Status"), opennow::Tr("Not connected"));
        AddInfoLine(overview, opennow::Tr("Next step"), opennow::Tr("Sign in from Library"));
    }
    else
    {
        const AuthSession& session = *state.session();
        const bool password_saved = client_.LoadNativeCredentials(session.provider.idp_id).has_value();
        AddInfoLine(overview, opennow::Tr("User"), session.user.display_name);
        auto* membership_value = AddInfoLine(
            overview,
            opennow::Tr("Membership"),
            session.user.membership_tier_verified
                ? Tr(session.user.membership_tier)
                : Tr("Unknown"));
        membership_value->setTextColor(membership::TextColor(
            session.user.membership_tier_verified
                ? session.user.membership_tier
                : std::string {}));
        AddInfoLine(overview, opennow::Tr("Provider"), session.provider.display_name);
        AddInfoLine(overview, opennow::Tr("Authentication"), FormatTokenState(session));
        AddInfoLine(overview, opennow::Tr("Quick sign-in"), password_saved ? opennow::Tr("Password saved") : opennow::Tr("Password not saved"));
    }
    AddInfoLine(overview, opennow::Tr("Saved accounts"), std::to_string(client_.LoadSavedSessions().size()));
    content_container_->addView(overview);

    if (state.HasSession())
    {
        const AuthSession& active = *state.session();
        auto* subscription_section = MakeSection(
            opennow::Tr("GeForce NOW subscription"),
            opennow::Tr("Usage information reported by NVIDIA for this account."));
        auto* tier_value = AddInfoLine(
            subscription_section,
            opennow::Tr("Service level"),
            active.user.membership_tier_verified
                ? active.user.membership_tier
                : Tr("Unknown"));
        tier_value->setTextColor(membership::TextColor(
            active.user.membership_tier_verified
                ? active.user.membership_tier
                : std::string {}));
        AddInfoLine(
            subscription_section,
            opennow::Tr("Time remaining"),
            subscription::FormatTimeRemaining(active.subscription));
        AddInfoLine(
            subscription_section,
            opennow::Tr("Cloud storage remaining"),
            subscription::FormatStorageRemaining(active.subscription));
        if (active.subscription.has_storage)
        {
            AddInfoLine(
                subscription_section,
                opennow::Tr("Cloud storage usage"),
                subscription::FormatStorageUsage(active.subscription));
        }
        content_container_->addView(subscription_section);
    }

    auto* session = MakeSection(opennow::Tr("Session"), opennow::Tr("Account actions take effect immediately and do not require X."));
    session->addView(MakeActionRow(
        opennow::Tr("Session details"), opennow::Tr("Identity, membership, provider and token lifetime."), opennow::Tr("View"),
        [this](brls::View* view) { return ShowSessionDialog(view); }));
    session->addView(MakeActionRow(
        opennow::Tr("Refresh authorization"), opennow::Tr("Renew the token and rotate the encrypted vault now."), opennow::Tr("Refresh"),
        [this](brls::View* view) { return TestTokenRefresh(view); }));
    session->addView(MakeActionRow(
        opennow::Tr("Choose saved account"), opennow::Tr("Switch profiles without repeating sign-in."), opennow::Tr("Choose"),
        [this](brls::View* view) { return SwitchSavedAccount(view); }));
    session->addView(MakeActionRow(
        opennow::Tr("Forget saved passwords"), opennow::Tr("Keep OAuth sessions, remove quick-login credentials."), opennow::Tr("Forget"),
        [this](brls::View* view) { return ClearSavedPasswords(view); }));
    content_container_->addView(session);

    auto* danger = MakeSection(opennow::Tr("Account removal"), opennow::Tr("These actions also clear the matching cached library state."));
    danger->addView(MakeActionRow(
        opennow::Tr("Remove active account"), opennow::Tr("Disconnect the current account and select another saved profile."), opennow::Tr("Remove"),
        [this](brls::View* view) { return ClearSavedLogin(view); }, true));
    danger->addView(MakeActionRow(
        opennow::Tr("Remove every account"), opennow::Tr("Delete all saved sessions and passwords from this console."), opennow::Tr("Remove all"),
        [this](brls::View* view) { return ClearAllSavedLogins(view); }, true));
    content_container_->addView(danger);
}

void SettingsTab::BuildStreamPage()
{
    auto* location = MakeSection(
        opennow::Tr("Server location"),
        opennow::Tr("Use automatic routing or choose a specific GeForce NOW data center."));
    location->addView(MakeOptionRow(
        opennow::Tr("Location"),
        opennow::Tr("Auto balances connection quality and fresh queue estimates. Closest prioritizes ping."),
        [this] { return ServerLocationValue(); },
        [this](brls::View* view) { return ChooseServerLocation(view); }));
    location->addView(MakeActionRow(
        opennow::Tr("Latency test"),
        opennow::Tr("Reload available locations and test each connection again."),
        opennow::Tr("Refresh"),
        [this](brls::View* view) { return RefreshServerLocations(view); }));
    content_container_->addView(location);

    auto* connection = MakeSection(opennow::Tr("Proxy"), opennow::Tr("Your proxy handles CloudMatch session requests. Video, audio and input stay direct."));
    connection->addView(MakeOptionRow(opennow::Tr("Use proxy"), opennow::Tr("Enter your proxy address, then enable and save with X."),
        [this] { return draft_settings_.proxy_enabled ? std::string("Enabled") : std::string("Disabled"); },
        [this](brls::View* view) { return ToggleProxy(view); }));
    connection->addView(MakeOptionRow(opennow::Tr("Proxy address"), opennow::Tr("HTTP, SOCKS5 or SOCKS5h. Include host and port; enter credentials below."),
        [this] { return draft_settings_.proxy_url.empty() ? std::string("Not configured") : draft_settings_.proxy_url; },
        [this](brls::View* view) { return EditProxy(view, 0); }));
    connection->addView(MakeOptionRow(opennow::Tr("Proxy username"), opennow::Tr("Optional authentication."),
        [this] { return draft_settings_.proxy_username.empty() ? std::string("Not configured") : std::string("Configured"); },
        [this](brls::View* view) { return EditProxy(view, 1); }));
    connection->addView(MakeOptionRow(opennow::Tr("Proxy password"), opennow::Tr("Leave empty to clear. Saved on this SD card."),
        [this] { return draft_settings_.proxy_password.empty() ? std::string("Not configured") : std::string("********"); },
        [this](brls::View* view) { return EditProxy(view, 2); }));
    content_container_->addView(connection);

    auto* quality = MakeSection(opennow::Tr("Quality profile"), opennow::Tr("Start with a preset, then fine-tune individual values."));
    quality->addView(MakeOptionRow(
        opennow::Tr("Preset"), opennow::Tr("Safe, Balanced or Quality baseline."),
        [this] { return draft_settings_.label; },
        [this](brls::View* view) { return CycleStreamPreset(view); }));
    quality->addView(MakeOptionRow(
        opennow::Tr("Resolution"), opennow::Tr("720p reduces load; 1080p improves detail."),
        [this] { return std::to_string(draft_settings_.width) + " x " + std::to_string(draft_settings_.height); },
        [this](brls::View* view) { return CycleResolution(view); }));
    quality->addView(MakeOptionRow(
        opennow::Tr("Frame rate"), opennow::Tr("60 FPS is smoother; 30 FPS is more resilient."),
        [this] { return std::to_string(draft_settings_.fps) + " FPS"; },
        [this](brls::View* view) { return CycleFrameRate(view); }));
    quality->addView(MakeOptionRow(
        opennow::Tr("Maximum bitrate"), opennow::Tr("Higher values improve motion detail but need stronger Wi-Fi."),
        [this] { return std::to_string(draft_settings_.bitrate_kbps / 1000) + " Mbps"; },
        [this](brls::View* view) { return CycleBitrate(view); }));
    quality->addView(MakeOptionRow(
        opennow::Tr("Streaming mode"),
        opennow::Tr("Performance prioritizes response; Balanced adapts delivery; Quality favors detail. Resolution and FPS stay configurable."),
        [this] { return Tr(draft_settings_.image_quality_mode); },
        [this](brls::View* view) { return CycleImageQuality(view); }));
    content_container_->addView(quality);

    auto* decoder = MakeSection(opennow::Tr("Decoder & delivery"), opennow::Tr("Auto uses NVDEC with a stable copied frame; NVDEC enables experimental zero-copy."));
    decoder->addView(MakeOptionRow(
        opennow::Tr("Video backend"), opennow::Tr("Choose safe automatic NVDEC, experimental zero-copy, or software decode."),
        [this] { return draft_settings_.video_backend; },
        [this](brls::View* view) { return CycleVideoBackend(view); }));
    AddInfoLine(decoder, opennow::Tr("Codec"), opennow::Tr("H.264, 8-bit 4:2:0"));
    content_container_->addView(decoder);

    auto* defaults = MakeSection(opennow::Tr("Recovery"), opennow::Tr("A known-good baseline for Nintendo Switch hardware."));
    defaults->addView(MakeActionRow(
        opennow::Tr("Restore Balanced defaults"), opennow::Tr("720p, 60 FPS, 12 Mbps, safe automatic NVDEC."), opennow::Tr("Restore"),
        [this](brls::View* view) { return ResetStreamPreset(view); }));
    content_container_->addView(defaults);

    auto* diagnostics = MakeSection(
        opennow::Tr("Diagnostics"),
        opennow::Tr("Disabled by default to avoid file I/O and keep the stream screen clean."));
    diagnostics->addView(MakeOptionRow(
        opennow::Tr("Debug diagnostics"),
        opennow::Tr("Show pre-stream telemetry and IN/DEC/OUT, and record detailed auth, session, stream, input, audio, render and UI logs."),
        [this] { return draft_settings_.debug_diagnostics ? std::string("Enabled") : std::string("Disabled"); },
        [this](brls::View* view) { return ToggleDebugDiagnostics(view); }));
    diagnostics->addView(MakeOptionRow(
        opennow::Tr("Stream statistics"),
        opennow::Tr("Show FPS, bitrate, ping, packet loss and connection state during play."),
        [this] { return draft_settings_.stats_overlay_enabled ? std::string("Enabled") : std::string("Disabled"); },
        [this](brls::View* view) { return ToggleStatsOverlay(view); }));
    content_container_->addView(diagnostics);
}

void SettingsTab::BuildAudioPage()
{
    auto* output = MakeSection(opennow::Tr("Stream audio"), opennow::Tr("Audio remains synchronized with the currently presented video frame."));
    output->addView(MakeOptionRow(
        opennow::Tr("Audio output"), opennow::Tr("Enable or mute GeForce NOW audio."),
        [this] { return draft_settings_.audio_enabled ? std::string("Enabled") : std::string("Muted"); },
        [this](brls::View* view) { return ToggleAudio(view); }));
    output->addView(MakeOptionRow(
        opennow::Tr("Volume boost"), opennow::Tr("Compensates for NVIDIA's quiet decoded PCM level."),
        [this] { return std::to_string(draft_settings_.audio_volume / 100) + "x"; },
        [this](brls::View* view) { return CycleAudioVolume(view); }));
    output->addView(MakeOptionRow(
        opennow::Tr("Playback buffer"), opennow::Tr("A larger buffer resists crackle but adds a little latency."),
        [this] { return std::to_string(draft_settings_.audio_buffer_ms) + " ms"; },
        [this](brls::View* view) { return CycleAudioBuffer(view); }));
    content_container_->addView(output);

    auto* format = MakeSection(opennow::Tr("Output format"), opennow::Tr("Selected automatically for Switch Audren."));
    AddInfoLine(format, opennow::Tr("Source"), opennow::Tr("Opus stereo"));
    AddInfoLine(format, opennow::Tr("Mixer"), opennow::Tr("48 kHz PCM"));
    AddInfoLine(format, opennow::Tr("Synchronization"), opennow::Tr("RTCP video clock"));
    content_container_->addView(format);
}

void SettingsTab::BuildGamePage()
{
    auto* game = MakeSection(
        opennow::Tr("Game preferences"),
        opennow::Tr("These values are sent to NVIDIA when a new GeForce NOW session is created."));
    game->addView(MakeOptionRow(
        opennow::Tr("Game language"),
        opennow::Tr("Language for in-game menus, subtitles and audio when the game supports it."),
        [this] { return GameLanguageLabel(draft_settings_.game_language); },
        [this](brls::View* view) { return ChooseGameLanguage(view); }));
    game->addView(MakeOptionRow(
        opennow::Tr("Save in-game graphics settings"),
        opennow::Tr("Allow supported games to retain graphics options changed inside the stream."),
        [this] {
            return draft_settings_.persist_game_settings
                ? std::string("Enabled")
                : std::string("Disabled");
        },
        [this](brls::View* view) { return TogglePersistGameSettings(view); }));
    content_container_->addView(game);
}

void SettingsTab::BuildControlsPage()
{
    auto* layout = MakeSection(
        opennow::Tr("Controller layout"),
        opennow::Tr("Only ABXY changes. Sticks, triggers, D-pad, hotkeys and system actions keep their current mapping."));
    layout->addView(MakeOptionRow(
        opennow::Tr("Face buttons"),
        opennow::Tr("Xbox keeps the current positional layout; Switch makes physical labels match the remote buttons."),
        [this] { return draft_settings_.controller_layout; },
        [this](brls::View* view) { return ToggleControllerLayout(view); }));
    AddInfoLine(
        layout,
        opennow::Tr("Xbox mode"),
        opennow::Tr("Switch A/B/X/Y -> Xbox B/A/Y/X"));
    AddInfoLine(
        layout,
        opennow::Tr("Switch mode"),
        opennow::Tr("Switch A/B/X/Y -> Xbox A/B/X/Y"));
    content_container_->addView(layout);
}

void SettingsTab::BuildStoragePage()
{
    const auto& state = AppState::Instance();
    const ImageCacheInfo images = ReadImageCacheInfo();
    auto* cache = MakeSection(opennow::Tr("Cover cache"), opennow::Tr("Artwork is stored on the SD card to keep the library responsive."));
    AddInfoLine(cache, opennow::Tr("Cover files"), std::to_string(images.files));
    AddInfoLine(cache, opennow::Tr("Disk usage"), FormatBytes(images.bytes));
    AddInfoLine(cache, opennow::Tr("Public catalog"), state.HasPublicGames() ? opennow::Tr("Loaded") : opennow::Tr("Not loaded"));
    AddInfoLine(cache, opennow::Tr("Owned library"), state.HasLibraryGames() ? opennow::Tr("Loaded") : opennow::Tr("Not loaded"));
    cache->addView(MakeActionRow(
        opennow::Tr("Inspect shared cache"), opennow::Tr("Show provider, catalog, library and artwork state."), opennow::Tr("Inspect"),
        [this](brls::View* view) { return ShowCacheState(view); }));
    cache->addView(MakeActionRow(
        opennow::Tr("Clear cover artwork"), opennow::Tr("Remove downloaded covers; they will be fetched again on demand."), opennow::Tr("Clear"),
        [this](brls::View* view) { return ClearCoverCache(view); }, true));
    content_container_->addView(cache);

    auto* system = MakeSection(opennow::Tr("System"), opennow::Tr("Information useful when diagnosing a hardware or network problem."));
    AddInfoLine(system, opennow::Tr("Applet mode"), AppletTypeName());
    AddInfoLine(system, opennow::Tr("Local IP"), NetworkUtils::GetLocalIPAddress());
    system->addView(MakeActionRow(
        opennow::Tr("Open diagnostics"), opennow::Tr("Inspect runtime mode, stream settings, logs and storage paths."), opennow::Tr("Open"),
        [this](brls::View* view) { return ShowDiagnostics(view); }));
    system->addView(MakeActionRow(
        opennow::Tr("Save diagnostic report"), opennow::Tr("Save filtered logs and session metrics to an SD card ZIP. No upload."), opennow::Tr("Save"),
        [](brls::View*) { SaveDiagnosticReportDialog(); return true; }));
    content_container_->addView(system);
}

void SettingsTab::BuildInterfacePage()
{
    auto* language = MakeSection(
        opennow::Tr("Language"),
        opennow::Tr("Choose the launcher language."));
    language->addView(MakeOptionRow(
        opennow::Tr("Language"),
        opennow::Tr("Choose the launcher language."),
        [this] { return InterfaceLanguageLabel(draft_settings_.interface_language); },
        [this](brls::View* view) { return ChooseInterfaceLanguage(view); }));
    content_container_->addView(language);

    auto* updates = MakeSection(
        opennow::Tr("Updates"),
        opennow::Tr("Updates are checked only when you choose this action."));
    AddInfoLine(updates, opennow::Tr("Installed version"), kAppVersion);
    updates->addView(MakeActionRow(
        opennow::Tr("Check for updates"),
        opennow::Tr("Check the official SwitchNOW GitHub release and install a newer NRO safely."),
        opennow::Tr("Check"),
        [this](brls::View* view) { return CheckForUpdates(view); }));
    content_container_->addView(updates);

    auto* about = MakeSection("SwitchNOW", opennow::Tr("Native GeForce NOW client for Nintendo Switch."));
    AddInfoLine(about, opennow::Tr("Version"), kAppVersion);
    AddInfoLine(about, opennow::Tr("Languages"), opennow::Tr("English + 7"));
    content_container_->addView(about);
}

bool SettingsTab::CheckForUpdates(brls::View* view)
{
    (void)view;
    const auto alive = alive_;
    brls::Application::notify(Tr("Checking for updates..."));
    LogRuntimeEvent("update", "check", std::string("current=") + kAppVersion);

    brls::async(
        [alive] {
            update::CheckResult result = update::CheckLatestRelease(kAppVersion);
            brls::sync([alive, result = std::move(result)]() mutable {
                if (!alive->load(std::memory_order_acquire))
                    return;
                if (!result.ok)
                {
                    LogRuntimeEvent("update", "check_failed", result.error);
                    ShowError(Tr("Update check failed"), result.error);
                    return;
                }
                if (!result.update_available)
                {
                    LogRuntimeEvent(
                        "update", "up_to_date",
                        std::string("current=") + kAppVersion +
                            " latest=" + result.release.version);
                    ShowDialog(
                        Tr("SwitchNOW is up to date"),
                        TrFormat(
                            "You already have the latest version ({0}).",
                            {kAppVersion}));
                    return;
                }

                const update::ReleaseInfo release = result.release;
                auto* dialog = new brls::Dialog(
                    Tr("Update available") + "\n\n" +
                    TrFormat(
                        "Version {0} is available. Installed version: {1}.",
                        {release.version, kAppVersion}) + "\n\n" +
                    Tr("Download and install it now?"));
                dialog->addButton(Tr("Install"), [alive, release] {
                    if (!alive->load(std::memory_order_acquire))
                        return;
                    brls::Application::notify(Tr("Downloading update..."));
                    brls::async(
                        [alive, release] {
                            std::string error;
                            const bool staged = update::StageRelease(
                                release, {}, error);
                            brls::sync([alive, release, staged, error] {
                                if (!alive->load(std::memory_order_acquire))
                                    return;
                                if (!staged)
                                {
                                    LogRuntimeEvent(
                                        "update", "stage_failed", error);
                                    ShowError(Tr("Update installation failed"), error);
                                    return;
                                }

                                std::string launch_error;
                                if (!update::LaunchUpdateHelper(launch_error))
                                    ShowError(Tr("Update installation failed"), launch_error);
                            });
                        },
                        false);
                });
                dialog->addButton(Tr("Later"), [] {});
                dialog->setCancelable(true);
                dialog->open();
            });
        },
        false);
    return true;
}

void SettingsTab::UpdateOptionValues()
{
    for (auto& [button, value] : option_values_)
    {
        if (button && value)
            button->setText(Tr(value()));
    }
}

void SettingsTab::MarkDirty()
{
    dirty_ = !SameSettings(draft_settings_, saved_settings_);
    RefreshSummary();
}

bool SettingsTab::SaveChanges(brls::View* view)
{
    (void)view;
    if (!settings_loaded_ || !dirty_)
    {
        brls::Application::notify(opennow::Tr("Settings are already up to date"));
        return true;
    }

    if (!SaveStreamSettings(draft_settings_))
    {
        ShowError(opennow::Tr("Settings Save Failed"), opennow::Tr("Could not safely write stream_settings.json to the SD card."));
        return true;
    }

    saved_settings_ = LoadStreamSettings();
    proxy_diagnostics::Configuration(saved_settings_);
    draft_settings_ = saved_settings_;
    SetInterfaceLanguage(saved_settings_.interface_language);
    title_label_->setText(Tr("Settings"));
    hint_label_->setText(Tr("Console-first controls. Press X to save, Y to revert."));
    nav_label_->setText(Tr("CATEGORIES"));
    nav_hint_->setText(Tr("Changes apply to the next stream. Account and cache actions run immediately."));
    SetStreamDiagnosticsEnabled(saved_settings_.debug_diagnostics);
    dirty_ = false;
    RefreshSummary();
    UpdateOptionValues();
    const std::array<const char*, 7> category_names {
        "Account", "Stream", "Game", "Controls", "Audio", "Storage", "Interface"};
    for (size_t index = 0; index < category_buttons_.size() && index < category_names.size(); ++index)
        category_buttons_[index]->setText(Tr(category_names[index]));

    // Every save can change labels or summaries. Rebuild only after the X
    // callback returns, and always move focus away from rows being destroyed.
    ScheduleCategoryRebuild();
    brls::Application::notify(Tr("Settings saved; changes apply to the next stream"));
    return true;
}

bool SettingsTab::RevertChanges(brls::View* view)
{
    (void)view;
    draft_settings_ = saved_settings_;
    dirty_ = false;
    RefreshSummary();
    UpdateOptionValues();
    brls::Application::notify(Tr("Unsaved changes reverted"));
    return true;
}

bool SettingsTab::CycleResolution(brls::View* view)
{
    (void)view;
    settings::CycleResolution(draft_settings_);
    MarkDirty();
    return true;
}

bool SettingsTab::CycleFrameRate(brls::View* view)
{
    (void)view;
    settings::CycleFrameRate(draft_settings_);
    MarkDirty();
    return true;
}

bool SettingsTab::CycleBitrate(brls::View* view)
{
    (void)view;
    settings::CycleBitrate(draft_settings_);
    MarkDirty();
    return true;
}

void SettingsTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    EnsureSessionLoaded();
    if (!settings_loaded_)
    {
        saved_settings_ = LoadStreamSettings();
        draft_settings_ = saved_settings_;
        settings_loaded_ = true;
    }
    RefreshSummary();
    RebuildCategory();
}

void SettingsTab::EnsureSessionLoaded()
{
    auto& state = AppState::Instance();
    if (state.IsSessionLoaded())
        return;

    AuthSession session;
    if (client_.LoadSavedSession(session))
        state.SetSession(std::move(session));
    else
        state.MarkSessionLoaded();
}

void SettingsTab::RefreshSummary()
{
    if (!save_status_)
        return;

    if (dirty_)
    {
        save_status_->setText(Tr("Unsaved changes  |  X Save"));
        save_status_->setTextColor(nvgRGB(255, 190, 92));
    }
    else
    {
        save_status_->setText(Tr("All changes saved"));
        save_status_->setTextColor(nvgRGB(88, 230, 146));
    }
}

bool SettingsTab::ShowSessionDialog(brls::View* view)
{
    (void)view;

    const auto& state = AppState::Instance();
    if (!state.HasSession())
    {
        ShowDialog(
            opennow::Tr("Session Details"),
            opennow::Tr("No saved GeForce NOW login is available on this Switch."));
        return true;
    }

    const AuthSession& session = *state.session();
    ShowDialog(
        opennow::Tr("Session Details"),
        opennow::Tr("User: ") + session.user.display_name + "\n" +
            opennow::Tr("Email: ") + (session.user.email.empty() ? std::string(opennow::Tr("Unavailable")) : session.user.email) + "\n" +
            opennow::Tr("Tier: ") + (session.user.membership_tier_verified
                ? Tr(session.user.membership_tier)
                : Tr("Unknown")) + "\n" +
            opennow::Tr("Provider: ") + session.provider.display_name + "\n" +
            opennow::Tr("Authentication: ") + FormatTokenState(session) + "\n" +
            opennow::Tr("Streaming base: ") + session.provider.streaming_service_url + "\n" +
            opennow::Tr("Saved accounts: ") + std::to_string(client_.LoadSavedSessions().size()));
    return true;
}

bool SettingsTab::TestTokenRefresh(brls::View* view)
{
    (void)view;
    auto& state = AppState::Instance();
    if (!state.HasSession())
    {
        ShowDialog(opennow::Tr("Token Refresh Test"), opennow::Tr("No GeForce NOW account is connected."));
        return true;
    }

    try
    {
        AuthSession refreshed = client_.ForceRefreshSavedSession(*state.session());
        refreshed.reauthentication_required = false;
        state.SetSession(std::move(refreshed));
        RefreshSummary();
        ScheduleCategoryRebuild();
        brls::Application::notify(opennow::Tr("Token refresh and vault rotation completed successfully"));
    }
    catch (const ReauthenticationRequired& e)
    {
        AuthSession invalid = *state.session();
        invalid.reauthentication_required = true;
        state.SetSession(std::move(invalid));
        RefreshSummary();
        ScheduleCategoryRebuild();
        ShowError(opennow::Tr("Token Refresh Requires Login"), e.what());
    }
    catch (const std::exception& e)
    {
        ShowError(opennow::Tr("Token Refresh Test Failed"), e.what());
    }
    return true;
}

bool SettingsTab::ClearSavedLogin(brls::View* view)
{
    (void)view;

    const auto& current_state = AppState::Instance();
    const std::string provider_id = current_state.HasSession()
        ? current_state.session()->provider.idp_id
        : std::string {};
    client_.ClearSavedSession();
    if (!provider_id.empty())
        client_.ClearNativeCredentials(provider_id);

    auto& state = AppState::Instance();
    state.SetLibraryGames({});

    AuthSession next;
    if (client_.LoadSavedSession(next))
    {
        state.SetSession(std::move(next));
        brls::Application::notify(opennow::Tr("Active login cleared; switched to another saved account"));
    }
    else
    {
        state.ClearSession();
        brls::Application::notify(opennow::Tr("Saved GeForce NOW login cleared"));
    }

    RefreshSummary();
    ScheduleCategoryRebuild();
    return true;
}

bool SettingsTab::SwitchSavedAccount(brls::View* view)
{
    (void)view;

    std::vector<AuthSession> sessions = client_.LoadSavedSessions();
    if (sessions.empty())
    {
        ShowDialog(opennow::Tr("Saved Accounts"), opennow::Tr("No saved GeForce NOW accounts are available."));
        return true;
    }

    const std::string active_user_id =
        AppState::Instance().HasSession() ? AppState::Instance().session()->user.user_id : "";

    auto* dialog = new brls::Dialog(opennow::Tr("Choose the GeForce NOW account to use."));
    for (const AuthSession& session : sessions)
    {
        const bool active = session.user.user_id == active_user_id;
        const std::string label = (active ? Tr("Active: ") : Tr("Use: ")) + session.user.display_name;
        dialog->addButton(label, [this, session]() {
            if (!client_.SetActiveSavedSession(session.user.user_id))
            {
                ShowError(opennow::Tr("Account Switch Failed"), opennow::Tr("Unable to activate the selected saved account."));
                return;
            }

            auto& state = AppState::Instance();
            state.SetSession(session);
            state.SetLibraryGames({});
            RefreshSummary();
            ScheduleCategoryRebuild();
            brls::Application::notify(opennow::Tr("Switched to ") + session.user.display_name);
        });
    }
    dialog->addButton(opennow::Tr("Cancel"), [] {});
    dialog->open();
    return true;
}

bool SettingsTab::ClearAllSavedLogins(brls::View* view)
{
    (void)view;

    client_.ClearAllSavedSessions();
    client_.ClearAllNativeCredentials();
    auto& state = AppState::Instance();
    state.ClearSession();
    state.SetLibraryGames({});
    RefreshSummary();
    ScheduleCategoryRebuild();
    brls::Application::notify(opennow::Tr("All saved GeForce NOW logins cleared"));
    return true;
}

bool SettingsTab::ClearSavedPasswords(brls::View* view)
{
    (void)view;
    client_.ClearAllNativeCredentials();
    ScheduleCategoryRebuild();
    brls::Application::notify(opennow::Tr("All saved NVIDIA passwords were removed; OAuth sessions remain connected"));
    return true;
}

bool SettingsTab::ShowCacheState(brls::View* view)
{
    (void)view;

    const auto& state = AppState::Instance();
    const ImageCacheInfo images = ReadImageCacheInfo();
    ShowDialog(
        opennow::Tr("Shared Cache"),
        opennow::Tr("Providers cached: ") + std::string(state.HasProviders() ? opennow::Tr("yes") : opennow::Tr("no")) + "\n" +
            opennow::Tr("Public catalog cached: ") + std::string(state.HasPublicGames() ? opennow::Tr("yes") : opennow::Tr("no")) + "\n" +
            opennow::Tr("Library cached: ") + std::string(state.HasLibraryGames() ? opennow::Tr("yes") : opennow::Tr("no")) + "\n" +
            opennow::Tr("Cover files: ") + std::to_string(images.files) + "\n" +
            opennow::Tr("Cover cache size: ") + FormatBytes(images.bytes));
    return true;
}

bool SettingsTab::ClearCoverCache(brls::View* view)
{
    (void)view;

    const size_t removed = ClearImageCacheFiles();
    RefreshSummary();
    ScheduleCategoryRebuild();
    brls::Application::notify(opennow::Tr("Removed ") + std::to_string(removed) + opennow::Tr(" cached cover files"));
    return true;
}

std::string SettingsTab::ServerLocationValue() const
{
    if (server_locations_loading_) return Tr("Testing locations...");
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const bool automatic = server_location::IsAutomatic(draft_settings_.region);
    const auto url = automatic ? server_zone::Select(server_locations_, draft_settings_.region, now) : draft_settings_.region;
    const auto selected = std::find_if(server_locations_.begin(), server_locations_.end(),
        [&](const auto& r) { return r.url == url; });
    std::string prefix = server_location::IsClosest(draft_settings_.region)
        ? Tr("Closest (lowest ping)") : Tr("Auto (best)");
    if (selected == server_locations_.end()) return automatic ? prefix : Tr("Selected server");
    std::string result = automatic ? prefix + " - " + selected->name : selected->name;
    if (selected->ping_ms >= 0) result += " - ~" + std::to_string(selected->ping_ms) + " ms";
    return result;
}

void SettingsTab::BeginServerLocationLoad(
    bool open_when_ready,
    bool notify_result)
{
    if (server_locations_loading_)
    {
        open_server_location_when_ready_ =
            open_server_location_when_ready_ || open_when_ready;
        if (notify_result)
            brls::Application::notify(Tr("Server location test is already running"));
        return;
    }

    EnsureSessionLoaded();
    auto& state = AppState::Instance();
    if (!state.HasSession())
    {
        if (open_when_ready || notify_result)
        {
            ShowError(
                Tr("Server Locations Unavailable"),
                Tr("Connect a GeForce NOW account before loading server locations."));
        }
        return;
    }

    server_locations_loading_ = true;
    server_locations_error_.clear();
    open_server_location_when_ready_ = open_when_ready;
    UpdateOptionValues();

    const std::shared_ptr<std::atomic_bool> alive = alive_;
    AuthSession session = *state.session();
    brls::async(
        [this, alive, session = std::move(session), notify_result]() mutable {
            try
            {
                GfnClient client;
                std::vector<StreamRegion> regions =
                    client.FetchStreamRegions(session);
                const std::vector<StreamRegion> cached =
                    client.LoadCachedStreamRegions(session);
                for (StreamRegion& region : regions)
                {
                    const auto match = std::find_if(
                        cached.begin(), cached.end(),
                        [&region](const StreamRegion& cached_region) {
                            return cached_region.url == region.url;
                        });
                    if (match != cached.end())
                    {
                        region.ping_ms = match->ping_ms;
                        region.jitter_ms = match->jitter_ms;
                        region.selection_score_ms =
                            match->selection_score_ms;
                        region.successful_probes =
                            match->successful_probes;
                        region.attempted_probes =
                            match->attempted_probes;
                    }
                }

                // Publish advertised locations immediately. Cached RTT values
                // make this useful before the background refinement finishes.
                std::vector<StreamRegion> immediate_regions = regions;
                std::stable_sort(
                    immediate_regions.begin(), immediate_regions.end(),
                    [](const StreamRegion& left, const StreamRegion& right) {
                        const bool left_ok = left.ping_ms >= 0;
                        const bool right_ok = right.ping_ms >= 0;
                        if (left_ok != right_ok)
                            return left_ok;
                        const int left_score =
                            server_location::RegionSelectionScore(left);
                        const int right_score =
                            server_location::RegionSelectionScore(right);
                        if (left_ok && left_score != right_score)
                            return left_score < right_score;
                        return left.name < right.name;
                    });
                brls::sync(
                    [this, alive,
                     regions = std::move(immediate_regions)]() mutable {
                        if (!alive->load(std::memory_order_acquire))
                            return;

                        server_locations_ = std::move(regions);
                        server_locations_loaded_ = true;
                        UpdateOptionValues();
                        const bool should_open =
                            open_server_location_when_ready_ &&
                            category_ == Category::Stream;
                        if (should_open)
                        {
                            open_server_location_when_ready_ = false;
                            ChooseServerLocation(nullptr);
                        }
                    });

                regions =
                    client.MeasureStreamRegionLatencies(std::move(regions), session.provider.streaming_service_url);
                client.SaveStreamRegionMeasurements(session, regions);
                std::stable_sort(
                    regions.begin(),
                    regions.end(),
                    [](const StreamRegion& left, const StreamRegion& right) {
                        const bool left_ok = left.ping_ms >= 0;
                        const bool right_ok = right.ping_ms >= 0;
                        if (left_ok != right_ok)
                            return left_ok;
                        const int left_score =
                            server_location::RegionSelectionScore(left);
                        const int right_score =
                            server_location::RegionSelectionScore(right);
                        if (left_ok && left_score != right_score)
                            return left_score < right_score;
                        return left.name < right.name;
                    });

                brls::sync(
                    [this, alive, session = std::move(session),
                     regions = std::move(regions), notify_result]() mutable {
                        if (!alive->load(std::memory_order_acquire))
                            return;

                        auto& current = AppState::Instance();
                        if (current.HasSession() &&
                            current.session()->user.user_id ==
                                session.user.user_id)
                        {
                            current.SetSession(std::move(session));
                        }

                        server_locations_ = std::move(regions);
                        server_locations_loaded_ = true;
                        server_locations_loading_ = false;
                        if (server_locations_.empty())
                        {
                            server_locations_error_ =
                                Tr("GeForce NOW did not return any server locations.");
                        }
                        UpdateOptionValues();

                        const bool should_open =
                            open_server_location_when_ready_ &&
                            category_ == Category::Stream;
                        open_server_location_when_ready_ = false;
                        if (should_open)
                        {
                            ChooseServerLocation(nullptr);
                        }
                        else if (notify_result)
                        {
                            brls::Application::notify(
                                server_locations_.empty()
                                    ? server_locations_error_
                                    : Tr("Server location latency test complete"));
                        }
                    });
            }
            catch (const std::exception& ex)
            {
                const std::string message = ex.what();
                brls::sync([this, alive, message, notify_result] {
                    if (!alive->load(std::memory_order_acquire))
                        return;

                    const bool should_open =
                        open_server_location_when_ready_ &&
                        category_ == Category::Stream;
                    server_locations_loading_ = false;
                    server_locations_loaded_ =
                        !server_locations_.empty() || should_open;
                    server_locations_error_ = message;
                    open_server_location_when_ready_ = false;
                    UpdateOptionValues();
                    if (should_open)
                    {
                        brls::Application::notify(
                            Tr("Latency test failed; automatic routing is still available"));
                        ChooseServerLocation(nullptr);
                    }
                    else if (notify_result)
                    {
                        ShowError(Tr("Server Location Test Failed"), message);
                    }
                });
            }
        },
        false);
}

bool SettingsTab::ChooseServerLocation(brls::View* view)
{
    (void)view;
    if (server_locations_loading_ && !server_locations_loaded_)
    {
        open_server_location_when_ready_ = true;
        brls::Application::notify(Tr("Loading GeForce NOW server locations..."));
        return true;
    }

    if (!server_locations_loaded_)
    {
        BeginServerLocationLoad(true, false);
        return true;
    }

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto best_url = server_zone::Select(server_locations_, "Auto", now);
    const auto closest_url = server_zone::Select(server_locations_, "Closest", now);
    std::vector<StreamRegion> options = server_locations_;
    if (!server_location::IsAutomatic(draft_settings_.region) &&
        std::none_of(options.begin(), options.end(), [this](const auto& r) { return r.url == draft_settings_.region; })) {
        StreamRegion current;
        current.name = Tr("Current server (not advertised)"); current.url = draft_settings_.region;
        options.push_back(std::move(current));
    }
    std::stable_sort(options.begin(), options.end(), [](const auto& a, const auto& b) {
        if (a.zone_id.empty() != b.zone_id.empty()) return !a.zone_id.empty();
        if ((a.ping_ms >= 0) != (b.ping_ms >= 0)) return a.ping_ms >= 0;
        if (a.ping_ms != b.ping_ms) return a.ping_ms < b.ping_ms;
        return a.name < b.name;
    });
    auto recommendation = [&](const char* title, const std::string& url) {
        std::string label = Tr(title);
        const auto r = std::find_if(options.begin(), options.end(), [&](const auto& item) { return item.url == url; });
        if (r != options.end()) label += " - " + r->name + " - ~" + std::to_string(r->ping_ms) + " ms";
        return label;
    };
    std::vector<std::string> labels {
        recommendation("Auto (best)", best_url), recommendation("Closest (lowest ping)", closest_url)};
    int selected_index = server_location::IsClosest(draft_settings_.region) ? 1 : 0;
    for (size_t i = 0; i < options.size(); ++i) {
        const auto& region = options[i];
        std::string label = region.name;
        if (region.ping_ms >= 0) label += " - ~" + std::to_string(region.ping_ms) + " ms";
        else label += " - " + Tr(server_locations_loading_ ? "Testing locations..." : "Unavailable");
        if (!region.zone_id.empty()) {
            label += " - " + (server_zone::FreshQueue(region, now)
                ? TrFormat("Public queue ~{0}", {std::to_string(region.zone_queue_estimate)})
                : Tr("Queue estimate unavailable"));
        }
        if (region.url == best_url) label += " - " + Tr("Best");
        labels.push_back(std::move(label));
        if (region.url == draft_settings_.region) selected_index = static_cast<int>(i + 2);
    }

    const std::shared_ptr<std::atomic_bool> alive = alive_;
    auto* dropdown = new brls::Dropdown(
        Tr("Server location"),
        labels,
        [this, alive, options = std::move(options)](int index) {
            if (!alive->load(std::memory_order_acquire) || index < 0)
                return;
            if (index == 0) draft_settings_.region = "Auto";
            else if (index == 1) draft_settings_.region = "Closest";
            else if (static_cast<size_t>(index - 2) < options.size())
                draft_settings_.region = options[static_cast<size_t>(index - 2)].url;
            else return;
            MarkDirty();
            UpdateOptionValues();
        },
        selected_index);
    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}

bool SettingsTab::RefreshServerLocations(brls::View* view)
{
    (void)view;
    BeginServerLocationLoad(false, true);
    return true;
}

bool SettingsTab::ShowDiagnostics(brls::View* view)
{
    (void)view;

    const auto& state = AppState::Instance();
    const ImageCacheInfo images = ReadImageCacheInfo();
    const StreamSettings stream_settings = LoadStreamSettings();
    const std::string local_ip = NetworkUtils::GetLocalIPAddress();

    std::string account = Tr("not connected");
    std::string provider = "n/a";
    if (state.HasSession())
    {
        account = state.session()->user.display_name;
        provider = state.session()->provider.display_name;
    }

    ShowDialog(
        opennow::Tr("Diagnostics"),
        opennow::Tr("Applet type: ") + AppletTypeName() + "\n" +
            opennow::Tr("Local IP: ") + local_ip + "\n" +
            opennow::Tr("Account: ") + account + "\n" +
            opennow::Tr("Provider: ") + provider + "\n" +
            opennow::Tr("Providers cached: ") + std::string(state.HasProviders() ? opennow::Tr("yes") : opennow::Tr("no")) + "\n" +
            opennow::Tr("Public catalog entries: ") + std::to_string(state.public_games().size()) + "\n" +
            opennow::Tr("Library entries: ") + std::to_string(state.library_games().size()) + "\n" +
            opennow::Tr("Stream: ") + (Tr(stream_settings.label)+" | "+std::to_string(stream_settings.width)+"x"+std::to_string(stream_settings.height)+
                " | "+std::to_string(stream_settings.fps)+" FPS | "+std::to_string(stream_settings.bitrate_kbps/1000)+" Mbps | "+
                Tr("Video backend")+": "+Tr(stream_settings.video_backend)+" | "+Tr("Region")+": "+Tr(stream_settings.region)+
                " | "+Tr("Game language")+": "+Tr(GameLanguageLabel(stream_settings.game_language))+" | "+
                Tr("Streaming mode")+": "+Tr(stream_settings.image_quality_mode)) + "\n" +
            opennow::Tr("Cover cache: ") + std::to_string(images.files) + opennow::Tr(" files, ") + FormatBytes(images.bytes) + "\n\n" +
            opennow::Tr("Paths:\n") +
            std::string(kAppHomePath) + "\n" +
            "boot.log: " + FileState(std::string(kAppHomePath) + "/boot.log") + "\n" +
            opennow::Tr("runtime.log (always on): ") +
                FileState(std::string(kAppHomePath) + "/runtime.log") + "\n" +
            "auth.log: " + FileState(std::string(kAppHomePath) + "/auth.log") + "\n" +
            "input.log: " + FileState(std::string(kAppHomePath) + "/input.log") + "\n" +
            "audio.log: " + FileState(std::string(kAppHomePath) + "/audio.log") + "\n" +
            "signaling.log: " + FileState(std::string(kAppHomePath) + "/signaling.log") + "\n" +
            "nte_autologin.log: " + FileState(std::string(kAppHomePath) + "/nte/nte_autologin.log") + "\n" +
            "auth_accounts.json: " + FileState(std::string(kAppHomePath) + "/auth_accounts.json") + "\n" +
            "active_cloud_session.json: " + FileState(std::string(kAppHomePath) + "/active_cloud_session.json") + "\n" +
            "stream_settings.json: " + FileState(std::string(kAppHomePath) + "/stream_settings.json") + "\n\n" +
            opennow::Tr("Tip: NVIDIA login needs Application mode. If Applet type is not Application/SystemApplication, launch hbmenu with title override."));
    return true;
}

} // namespace opennow
