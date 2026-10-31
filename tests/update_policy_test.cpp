#include "../app/src/update_policy.hpp"

#include <cassert>

int main()
{
    using opennow::update::IsNewerVersion;
    using opennow::update::IsTrustedReleaseAsset;

    assert(IsNewerVersion("v1.1.7-r8", "1.1.7-r7"));
    assert(IsNewerVersion("1.2.0", "1.1.99-r99"));
    assert(!IsNewerVersion("v1.1.5", "1.1.7-r8"));
    assert(!IsNewerVersion("1.1.7-r7", "1.1.7-r8"));
    assert(!IsNewerVersion("not-a-version", "1.1.7-r8"));
    assert(IsNewerVersion("v1.2.1-R18-test", "1.2.1 R17 test"));
    assert(IsNewerVersion("1.2.1", "1.2.1 R18 test"));
    assert(!IsNewerVersion("1.2.1-R17-test", "1.2.1 R18 test"));
    assert(!IsNewerVersion("1.2.1-R18-test", "1.2.1"));
    assert(!IsNewerVersion("1.2.1 R18 test", "1.2.1-R18-test"));
    assert(IsTrustedReleaseAsset(
        "https://github.com/Blade-Punisher/SwitchNOW/releases/download/v1.2.0/SwitchNOW-1.2.0.nro"));
    assert(!IsTrustedReleaseAsset("https://example.com/SwitchNOW.nro"));
    return 0;
}
