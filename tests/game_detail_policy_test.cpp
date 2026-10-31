#include "game_detail_policy.hpp"

#include <cassert>

int main()
{
    using namespace opennow::game_detail;

    assert(IsUnknownMetadata(""));
    assert(IsUnknownMetadata("UNKNOWN"));
    assert(IsUnknownMetadata("n/a"));
    assert(!IsUnknownMetadata("Steam"));
    assert(DisplayStore("UNKNOWN") == "GeForce NOW");
    assert(DisplayStore("Epic") == "Epic");
    assert(DetailSubtitle(true) == "In your GeForce NOW library");
    assert(DetailSubtitle(false) == "Available on GeForce NOW");
    assert(HeaderSubtitle(true) == "GeForce NOW library");
    assert(HeaderSubtitle(false) == "GeForce NOW catalog");
    assert(FormatLastPlayed("") == "Never");
    assert(FormatLastPlayed("2026-07-20T22:35:02Z") == "2026-07-20  ·  22:35 UTC");
    assert(FormatLastPlayed("2026-07-20T22:35:02+02:00") == "2026-07-20T22:35:02+02:00");
    assert(FormatLastPlayed("Recently") == "Recently");
    return 0;
}
