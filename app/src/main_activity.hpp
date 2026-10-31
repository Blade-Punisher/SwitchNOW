#pragma once

#include "home_shortcut.hpp"

#include <borealis.hpp>

#include <optional>

namespace opennow
{

class MainActivity : public brls::Activity
{
  public:
    explicit MainActivity(
        std::optional<shortcut::LaunchRequest> launch_request = std::nullopt,
        std::optional<shortcut::InstallerResult> installer_result = std::nullopt);
};

} // namespace opennow
