#include "gfn/app_launch_mode_policy.hpp"

#include <cassert>

static_assert(opennow::gfn::ConsoleAppLaunchModeWireValue() == 2);

int main()
{
    assert(opennow::gfn::ConsoleAppLaunchModeWireValue() == 2);
    return 0;
}
