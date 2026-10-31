#include "settings_tab.hpp"

#include "localization.hpp"
#include "proxy_policy.hpp"
#include "stream_settings_policy.hpp"
#include "ui_helpers.hpp"

namespace opennow
{

bool SettingsTab::CycleStreamPreset(brls::View* view)
{
    (void)view;
    const bool audio_enabled = draft_settings_.audio_enabled;
    const int audio_volume = draft_settings_.audio_volume;
    const int audio_buffer_ms = draft_settings_.audio_buffer_ms;
    const std::string backend = draft_settings_.video_backend;
    const std::string region = draft_settings_.region;
    const bool debug_diagnostics = draft_settings_.debug_diagnostics;
    const bool stats_overlay_enabled = draft_settings_.stats_overlay_enabled;
    const bool proxy_enabled = draft_settings_.proxy_enabled;
    const std::string proxy_url = draft_settings_.proxy_url;
    const auto proxy_username = draft_settings_.proxy_username;
    const auto proxy_password = draft_settings_.proxy_password;
    const std::string game_language = draft_settings_.game_language;
    const bool persist_game_settings = draft_settings_.persist_game_settings;
    const std::string controller_layout = draft_settings_.controller_layout;
    const std::string image_quality_mode = draft_settings_.image_quality_mode;
    const std::string interface_language = draft_settings_.interface_language;

    draft_settings_ = NextStreamPreset(draft_settings_);
    draft_settings_.audio_enabled = audio_enabled;
    draft_settings_.audio_volume = audio_volume;
    draft_settings_.audio_buffer_ms = audio_buffer_ms;
    draft_settings_.video_backend = backend;
    draft_settings_.region = region;
    draft_settings_.debug_diagnostics = debug_diagnostics;
    draft_settings_.stats_overlay_enabled = stats_overlay_enabled;
    draft_settings_.proxy_enabled = proxy_enabled;
    draft_settings_.proxy_url = proxy_url;
    draft_settings_.proxy_username = proxy_username;
    draft_settings_.proxy_password = proxy_password;
    draft_settings_.game_language = game_language;
    draft_settings_.persist_game_settings = persist_game_settings;
    draft_settings_.controller_layout = controller_layout;
    draft_settings_.image_quality_mode = image_quality_mode;
    draft_settings_.interface_language = interface_language;
    MarkDirty();
    return true;
}

bool SettingsTab::ResetStreamPreset(brls::View* view)
{
    (void)view;

    const std::string region = draft_settings_.region;
    const bool audio_enabled = draft_settings_.audio_enabled;
    const int audio_volume = draft_settings_.audio_volume;
    const int audio_buffer_ms = draft_settings_.audio_buffer_ms;
    const std::string backend = draft_settings_.video_backend;
    const bool debug_diagnostics = draft_settings_.debug_diagnostics;
    const bool stats_overlay_enabled = draft_settings_.stats_overlay_enabled;
    const bool proxy_enabled = draft_settings_.proxy_enabled;
    const std::string proxy_url = draft_settings_.proxy_url;
    const auto proxy_username = draft_settings_.proxy_username;
    const auto proxy_password = draft_settings_.proxy_password;
    const std::string game_language = draft_settings_.game_language;
    const bool persist_game_settings = draft_settings_.persist_game_settings;
    const std::string controller_layout = draft_settings_.controller_layout;
    const std::string image_quality_mode = draft_settings_.image_quality_mode;
    const std::string interface_language = draft_settings_.interface_language;
    draft_settings_ = StreamPresets()[1];
    draft_settings_.region = region;
    draft_settings_.audio_enabled = audio_enabled;
    draft_settings_.audio_volume = audio_volume;
    draft_settings_.audio_buffer_ms = audio_buffer_ms;
    draft_settings_.video_backend = backend;
    draft_settings_.debug_diagnostics = debug_diagnostics;
    draft_settings_.stats_overlay_enabled = stats_overlay_enabled;
    draft_settings_.proxy_enabled = proxy_enabled;
    draft_settings_.proxy_url = proxy_url;
    draft_settings_.proxy_username = proxy_username;
    draft_settings_.proxy_password = proxy_password;
    draft_settings_.game_language = game_language;
    draft_settings_.persist_game_settings = persist_game_settings;
    draft_settings_.controller_layout = controller_layout;
    draft_settings_.image_quality_mode = image_quality_mode;
    draft_settings_.interface_language = interface_language;
    MarkDirty();
    UpdateOptionValues();
    brls::Application::notify(opennow::Tr("Balanced defaults staged; press X to save"));
    return true;
}

bool SettingsTab::CycleVideoBackend(brls::View* view)
{
    (void)view;
    if (draft_settings_.video_backend == "Auto")
        draft_settings_.video_backend = "NVDEC";
    else if (draft_settings_.video_backend == "NVDEC")
        draft_settings_.video_backend = "Software";
    else
        draft_settings_.video_backend = "Auto";
    MarkDirty();
    return true;
}

bool SettingsTab::CycleImageQuality(brls::View* view)
{
    (void)view;
    settings::CycleImageQuality(draft_settings_);
    MarkDirty();
    return true;
}

bool SettingsTab::ChooseGameLanguage(brls::View* view)
{
    (void)view;
    const auto& options = GameLanguageOptions();
    std::vector<std::string> labels;
    labels.reserve(options.size());
    int selected = 0;
    for (size_t index = 0; index < options.size(); ++index)
    {
        labels.push_back(Tr(options[index].label));
        if (options[index].code == draft_settings_.game_language)
            selected = static_cast<int>(index);
    }

    auto* dropdown = new brls::Dropdown(
        opennow::Tr("Game language"), labels,
        [this](int index) {
            const auto& languages = GameLanguageOptions();
            if (index < 0 || static_cast<size_t>(index) >= languages.size())
                return;
            draft_settings_.game_language = languages[static_cast<size_t>(index)].code;
            MarkDirty();
            UpdateOptionValues();
        },
        selected);
    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}

bool SettingsTab::ChooseInterfaceLanguage(brls::View* view)
{
    (void)view;
    const auto& options = InterfaceLanguageOptions();
    std::vector<std::string> labels;
    labels.reserve(options.size());
    int selected = 0;
    for (size_t index = 0; index < options.size(); ++index)
    {
        labels.push_back(options[index].label);
        if (options[index].code == draft_settings_.interface_language)
            selected = static_cast<int>(index);
    }

    auto* dropdown = new brls::Dropdown(
        Tr("Language"), labels,
        [this](int index) {
            const auto& languages = InterfaceLanguageOptions();
            if (index < 0 || static_cast<size_t>(index) >= languages.size())
                return;
            draft_settings_.interface_language = languages[static_cast<size_t>(index)].code;
            MarkDirty();
            UpdateOptionValues();
        },
        selected);
    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}

bool SettingsTab::TogglePersistGameSettings(brls::View* view)
{
    (void)view;
    draft_settings_.persist_game_settings = !draft_settings_.persist_game_settings;
    MarkDirty();
    return true;
}

bool SettingsTab::ToggleControllerLayout(brls::View* view)
{
    (void)view;
    draft_settings_.controller_layout =
        draft_settings_.controller_layout == "Switch" ? "Xbox" : "Switch";
    MarkDirty();
    return true;
}

bool SettingsTab::ToggleAudio(brls::View* view)
{
    (void)view;
    draft_settings_.audio_enabled = !draft_settings_.audio_enabled;
    MarkDirty();
    return true;
}

bool SettingsTab::CycleAudioVolume(brls::View* view)
{
    (void)view;
    constexpr int gains[] = {100, 200, 400, 600, 800};
    int next_gain = gains[0];
    for (int gain : gains)
    {
        if (gain > draft_settings_.audio_volume)
        {
            next_gain = gain;
            break;
        }
    }
    draft_settings_.audio_volume = next_gain;
    MarkDirty();
    return true;
}

bool SettingsTab::CycleAudioBuffer(brls::View* view)
{
    (void)view;
    draft_settings_.audio_buffer_ms =
        draft_settings_.audio_buffer_ms >= 100 ? 30 : draft_settings_.audio_buffer_ms + 10;
    MarkDirty();
    return true;
}

bool SettingsTab::ToggleDebugDiagnostics(brls::View* view)
{
    (void)view;
    draft_settings_.debug_diagnostics = !draft_settings_.debug_diagnostics;
    MarkDirty();
    brls::Application::notify(
        draft_settings_.debug_diagnostics
            ? opennow::Tr("Debug diagnostics staged; press X to save")
            : opennow::Tr("Clean stream mode staged; press X to save"));
    return true;
}

bool SettingsTab::ToggleStatsOverlay(brls::View* view)
{
    (void)view;
    draft_settings_.stats_overlay_enabled = !draft_settings_.stats_overlay_enabled;
    MarkDirty();
    return true;
}

bool SettingsTab::ToggleProxy(brls::View* view) {
    (void)view;
    if (!draft_settings_.proxy_enabled && proxy::NormalizeUrl(draft_settings_.proxy_url).empty()) {
        ShowError(Tr("Proxy"), Tr("Enter a valid proxy address first.")); return true;
    }
    draft_settings_.proxy_enabled = !draft_settings_.proxy_enabled;
    MarkDirty(); UpdateOptionValues(); return true;
}
bool SettingsTab::EditProxy(brls::View* view, int field) {
    (void)view;
    const auto alive = alive_;
    const auto title = field == 0 ? "Proxy address" : field == 1 ? "Proxy username" : "Proxy password";
    const auto initial = field == 0 ? draft_settings_.proxy_url : field == 1 ? draft_settings_.proxy_username : std::string{};
    brls::Application::getImeManager()->openForText([this, alive, field](std::string value) {
        if (!alive->load()) return;
        if (field == 0) {
            const auto normalized = proxy::NormalizeUrl(value);
            if (!value.empty() && normalized.empty()) {
                ShowError(Tr("Proxy"), Tr("Use http://host:port, socks5://host:port or socks5h://host:port.")); return;
            }
            draft_settings_.proxy_url = normalized;
            if (normalized.empty()) draft_settings_.proxy_enabled = false;
        } else {
            if (!proxy::ValidCredential(value)) { ShowError(Tr("Proxy"), Tr("Invalid proxy credentials.")); return; }
            if (field == 1) draft_settings_.proxy_username = std::move(value);
            else draft_settings_.proxy_password = std::move(value);
        }
        MarkDirty(); UpdateOptionValues();
    }, Tr(title), field == 0 ? Tr("Use http://host:port, socks5://host:port or socks5h://host:port.") : Tr("Leave empty to clear."), field == 0 ? 512 : 256, initial);
    return true;
}
} // namespace opennow
