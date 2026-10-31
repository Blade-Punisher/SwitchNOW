#pragma once
#include <cstdint>
namespace opennow::input {
// Fresh input can defer routine UI maintenance, never indefinitely. Startup
// and handshake are exempt; additional deferral stops once maintenance is 50ms old.
constexpr bool DeferUiMaintenance(bool ready, bool pending, std::uint64_t now,
                                  std::uint64_t last) {
    return ready && pending && last && now >= last && now - last < 50000;
}
constexpr bool HeartbeatDue(std::uint64_t now, std::uint64_t last, bool pending) {
    if (!last || now < last) return true;
    const auto age = now - last;
    return age >= 2000000 && (!pending || age >= 2100000);
}
// Service at least one report; an expensive send must not turn a four-pad
// flush into four consecutive long calls before receiving more UDP packets.
constexpr bool EndInputBatch(unsigned sent, std::uint64_t elapsed_us) {
    return sent >= 1 && elapsed_us >= 2000;
}
}
