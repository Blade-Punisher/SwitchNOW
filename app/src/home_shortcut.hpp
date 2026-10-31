#pragma once

#include "home_shortcut_policy.hpp"

#include <cstddef>
#include "models.hpp"
#include <optional>
#include <string>

namespace opennow::shortcut
{

struct CreateResult
{
    bool success = false;
    bool used_game_cover = false;
    std::string title;
    std::string nro_path;
    std::string manifest_path;
    std::string error;
};

struct InstallerResult
{
    bool success = false;
    std::string title;
    std::string error;
};

struct RepairResult
{
    size_t scanned = 0;
    size_t repaired = 0;
    size_t failed = 0;
};

void SetExecutablePath(std::string path);
const std::string& ExecutablePath();
RepairResult RepairLegacyShortcutManifests();
std::optional<LaunchRequest> ReadLaunchRequest(int argc, char* argv[]);
std::optional<InstallerResult> ConsumeInstallerResult();
CreateResult CreateGameShortcut(LaunchRequest request);
bool RefreshGameShortcut(LaunchRequest& request, AuthSession session);
bool StartForwarderInstaller(
    const std::string& nro_path, const std::string& title,
    std::string& error, bool automatic = false);


} // namespace opennow::shortcut
