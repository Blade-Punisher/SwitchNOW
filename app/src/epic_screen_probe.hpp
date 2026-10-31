#pragma once
#include "epic_autologin.hpp"
#include <memory>
namespace opennow::autologin::epic {
// UI-thread state. A frame callback retains only this job, never a StreamView.
struct Probe {
    bool active=true,pending=false;
    std::uint64_t serial=0;
    Screen screen=Screen::Unknown;
    float continue_y=366.f/720;
    Flow::Clock::time_point requested_at{};
};
void RequestProbe(const std::shared_ptr<Probe>&,Flow::Clock::time_point);
} // namespace opennow::autologin::epic
