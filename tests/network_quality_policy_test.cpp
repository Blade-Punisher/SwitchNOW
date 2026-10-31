#include "network_quality_policy.hpp"

#include <cassert>

static_assert(opennow::network::WifiBandFromChannel(1) ==
              opennow::network::WifiBand::Ghz2_4);
static_assert(opennow::network::WifiBandFromChannel(36) ==
              opennow::network::WifiBand::Ghz5);

int main()
{
    assert(opennow::network::ShouldWarnForStreaming(
        opennow::network::WifiBand::Ghz2_4));
    assert(!opennow::network::ShouldWarnForStreaming(
        opennow::network::WifiBand::Ghz5));
    return 0;
}
