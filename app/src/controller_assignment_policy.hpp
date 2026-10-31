#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opennow::input
{

constexpr std::size_t kRemoteControllerCount = 4;
constexpr std::size_t kSwitchControllerSourceCount = 5;
constexpr std::int8_t kUnassignedController = -1;

class ControllerAssignments
{
  public:
    ControllerAssignments()
    {
        source_to_controller_.fill(kUnassignedController);
        controller_to_source_.fill(kUnassignedController);
    }

    std::int8_t Assign(std::size_t source)
    {
        if (source >= source_to_controller_.size())
            return kUnassignedController;
        if (source_to_controller_[source] != kUnassignedController)
            return source_to_controller_[source];

        // Handheld and Npad No1 both naturally prefer player one. If both are
        // active, whichever was observed first keeps it and the other takes
        // the next free position. Assignments remain reserved on disconnect.
        const std::size_t preferred = source == 0 ? 0 : source - 1;
        if (preferred < controller_to_source_.size() &&
            controller_to_source_[preferred] == kUnassignedController)
        {
            return Bind(source, preferred);
        }

        for (std::size_t controller = 0;
             controller < controller_to_source_.size(); ++controller)
        {
            if (controller_to_source_[controller] == kUnassignedController)
                return Bind(source, controller);
        }
        return kUnassignedController;
    }

    std::int8_t ControllerForSource(std::size_t source) const
    {
        return source < source_to_controller_.size()
            ? source_to_controller_[source]
            : kUnassignedController;
    }

  private:
    std::int8_t Bind(std::size_t source, std::size_t controller)
    {
        source_to_controller_[source] = static_cast<std::int8_t>(controller);
        controller_to_source_[controller] = static_cast<std::int8_t>(source);
        return static_cast<std::int8_t>(controller);
    }

    std::array<std::int8_t, kSwitchControllerSourceCount> source_to_controller_;
    std::array<std::int8_t, kRemoteControllerCount> controller_to_source_;
};

inline std::uint16_t ControllerBitmap(
    const std::array<bool, kRemoteControllerCount>& connected)
{
    std::uint16_t bitmap = 0;
    for (std::size_t controller = 0; controller < connected.size(); ++controller)
    {
        if (!connected[controller])
            continue;
        bitmap |= static_cast<std::uint16_t>(1u << controller);
        bitmap |= static_cast<std::uint16_t>(1u << (controller + 8));
    }
    return bitmap;
}

} // namespace opennow::input
