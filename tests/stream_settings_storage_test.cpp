#include "stream_settings.hpp"
#include "app_paths.hpp"
#include "server_zone_policy.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace opennow {
const std::string& AppHomePath() {
    static const std::string path = "build/host-tests/settings-storage-fixture";
    return path;
}
void PrepareAppStorage() { std::filesystem::create_directories(AppHomePath()); }
}
int main() {
    using namespace opennow;
    StreamSettings settings;
    settings.audio_volume = 600;
    settings.image_quality_mode = "Balanced";
    for (const auto& region : {std::string("Auto"), std::string("Closest"), server_zone::ZoneUrl("NP-WAW-01")}) {
        settings.region = region;
        assert(SaveStreamSettings(settings));
        const auto loaded = LoadStreamSettings();
        assert(loaded.region == region);
        assert(loaded.audio_volume == 600 && loaded.image_quality_mode == "Balanced");
    }
    settings.region = "http://unsafe.example/";
    assert(SaveStreamSettings(settings));
    assert(LoadStreamSettings().region == "Auto");
    settings.proxy_enabled=true; settings.proxy_url="SOCKS5H://localhost:1080/";
    settings.proxy_username="user@domain"; settings.proxy_password="secret:with%chars";
    assert(SaveStreamSettings(settings));
    auto loaded=LoadStreamSettings();
    assert(loaded.proxy_enabled && loaded.proxy_url=="socks5h://localhost:1080");
    assert(loaded.proxy_username==settings.proxy_username && loaded.proxy_password==settings.proxy_password);
    assert(FormatStreamSettings(loaded).find("secret")==std::string::npos);
    const auto path=AppHomePath()+"/stream_settings.json";
    std::ofstream(path) << R"({"community_proxy_enabled":true,"community_proxy_url":"http://legacy:secret@old.example:3128"})";
    loaded=LoadStreamSettings();
    assert(!loaded.proxy_enabled && loaded.proxy_url.empty() && loaded.proxy_password.empty());
    assert(SaveStreamSettings(loaded));
    std::ifstream input(path);const std::string json((std::istreambuf_iterator<char>(input)),{});
    assert(json.find("community_proxy")==std::string::npos && json.find("legacy")==std::string::npos);
    input.close();
    settings.proxy_enabled=true;settings.proxy_url="http://bad@address:80";
    assert(SaveStreamSettings(settings));assert(!LoadStreamSettings().proxy_enabled);

}
