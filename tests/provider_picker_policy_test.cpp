#include "provider_picker_policy.hpp"

#include <cassert>

int main()
{
    using namespace opennow;
    using namespace opennow::provider_picker;

    LoginProvider nvidia;
    nvidia.idp_id = "nvidia-id";
    nvidia.code = "NVIDIA";
    nvidia.display_name = "NVIDIA";
    assert(IsNvidia(nvidia));
    assert(DisplayName(nvidia) == "NVIDIA");
    assert(std::string(TypeLabel(nvidia)) == "NVIDIA");
    assert(std::string(SignInLabel(nvidia)).find("console") != std::string::npos);

    LoginProvider alliance;
    alliance.idp_id = "partner-id";
    alliance.code = "BPC";
    alliance.display_name = "bro.game";
    assert(!IsNvidia(alliance));
    assert(DisplayName(alliance) == "bro.game");
    assert(std::string(TypeLabel(alliance)) == "ALLIANCE");
    assert(std::string(SignInLabel(alliance)).find("phone") != std::string::npos);
    assert(IsCurrent(alliance, &alliance));
    assert(!IsCurrent(alliance, &nvidia));

    LoginProvider unnamed;
    assert(IsNvidia(unnamed));
    assert(DisplayName(unnamed) == "GeForce NOW");
    assert(ProviderCode(unnamed) == "NVIDIA");
    assert(std::string(Description(nvidia, true)).find("active") != std::string::npos);
    return 0;
}
