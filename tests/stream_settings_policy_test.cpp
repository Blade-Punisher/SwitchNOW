#include "stream_settings_policy.hpp"

#include <cassert>

int main()
{
    opennow::StreamSettings value;
    value.video_backend = "NVDEC";
    value.audio_volume = 1400;
    value.game_language = "ru_RU";
    value.persist_game_settings = false;
    value.controller_layout = "Switch";
    value.image_quality_mode = "Adaptive";

    opennow::settings::CycleResolution(value);
    assert(value.width == 1920 && value.height == 1080);
    assert(value.preset_id == "custom" && value.label == "Custom");
    assert(value.video_backend == "NVDEC" && value.audio_volume == 1400);
    assert(value.game_language == "ru_RU" && !value.persist_game_settings);
    assert(value.controller_layout == "Switch");
    assert(value.image_quality_mode == "Adaptive");

    opennow::settings::CycleResolution(value);
    assert(value.width == 1280 && value.height == 720);

    opennow::settings::CycleFrameRate(value);
    assert(value.fps == 30);
    opennow::settings::CycleFrameRate(value);
    assert(value.fps == 60);

    value.bitrate_kbps = 12000;
    opennow::settings::CycleBitrate(value);
    assert(value.bitrate_kbps == 16000);
    value.bitrate_kbps = 25000;
    opennow::settings::CycleBitrate(value);
    assert(value.bitrate_kbps == 8000);
    opennow::settings::CycleBitrate(value);
    assert(value.bitrate_kbps == 10000);

    opennow::settings::CycleImageQuality(value);
    assert(value.image_quality_mode == "Quality");
    opennow::settings::CycleImageQuality(value);
    assert(value.image_quality_mode == "Performance");
    opennow::settings::CycleImageQuality(value);
    assert(value.image_quality_mode == "Balanced");
    return 0;
}
