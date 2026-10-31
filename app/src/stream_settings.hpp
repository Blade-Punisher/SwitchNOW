#pragma once

#include <string>
#include <vector>

namespace opennow
{

struct StreamSettings
{
    std::string preset_id = "balanced";
    std::string label     = "Balanced";
    int width             = 1280;
    int height            = 720;
    int fps               = 60;
    int bitrate_kbps      = 12000;
    std::string codec     = "H264";
    std::string region    = "Auto";
    bool audio_enabled    = true;
    int audio_volume      = 600;
    int audio_buffer_ms   = 40;
    std::string video_backend = "Auto";
    bool debug_diagnostics = false;
    bool stats_overlay_enabled = false;
    std::string game_language = "en_US";
    bool persist_game_settings = true;
    std::string controller_layout = "Xbox";
    std::string image_quality_mode = "Balanced";
    std::string interface_language = "en";
    bool proxy_enabled = false;
    std::string proxy_url;
    std::string proxy_username;
    std::string proxy_password;
};

struct GameLanguageOption
{
    std::string code;
    std::string label;
};

const std::vector<StreamSettings>& StreamPresets();
const std::vector<GameLanguageOption>& GameLanguageOptions();
std::string GameLanguageLabel(const std::string& code);
StreamSettings LoadStreamSettings();
bool SaveStreamSettings(const StreamSettings& settings);
StreamSettings NextStreamPreset(const StreamSettings& current);
std::string FormatStreamSettings(const StreamSettings& settings);

} // namespace opennow
