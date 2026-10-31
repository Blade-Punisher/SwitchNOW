#include "controller_assignment_policy.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    using opennow::input::ControllerAssignments;
    using opennow::input::ControllerBitmap;

    ControllerAssignments docked;
    assert(docked.Assign(1) == 0);
    assert(docked.Assign(2) == 1);
    assert(docked.Assign(3) == 2);
    assert(docked.Assign(4) == 3);
    assert(docked.Assign(1) == 0);

    ControllerAssignments handheld;
    assert(handheld.Assign(0) == 0);
    assert(handheld.Assign(1) == 1);
    assert(handheld.Assign(2) == 2);
    assert(handheld.Assign(3) == 3);
    assert(handheld.Assign(4) == -1);

    // A reconnect uses the reserved player number instead of compacting the
    // remaining controllers into a different position.
    assert(handheld.ControllerForSource(2) == 2);
    assert(handheld.Assign(2) == 2);

    const std::array<bool, 4> connected = {true, false, true, true};
    assert(ControllerBitmap(connected) == static_cast<std::uint16_t>(0x0d0d));
    return 0;
}
