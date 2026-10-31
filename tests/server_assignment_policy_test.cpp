#include "server_assignment_policy.hpp"
#include <cassert>
int main() {
    using namespace opennow::routing;
    Assignment route;
    const std::string region = "https://eu-poland.cloudmatchbeta.nvidiagrid.net/";
    assert(!route.Observe("session-a",region,"np-waw-01.cloudmatchbeta.nvidiagrid.net"));
    assert(!route.Observe("session-a",{},{}));
    assert(route.Observe("session-a",region,"np-par-05.cloudmatchbeta.nvidiagrid.net"));
    assert(route.redirects == 1 && ShortHost(route.initial_host) == "np-waw-01");
    assert(ShortHost(route.actual_host) == "np-par-05");
    assert(!route.Observe("session-a",region,"NP-PAR-05.cloudmatchbeta.nvidiagrid.net"));
    assert(route.requested_host == "eu-poland.cloudmatchbeta.nvidiagrid.net");
    assert(route.requested_url == region);
    route.Observe("session-a", "https://80.84.174.149/", "np-par-05.cloudmatchbeta.nvidiagrid.net");
    assert(route.requested_url == region); // direct polling must not retrain the requested zone
    assert(route.Observe("session-a",region,"np-waw-01.cloudmatchbeta.nvidiagrid.net"));
    assert(route.redirects == 2);
    assert(!route.Observe("session-b",region,"np-waw-01.cloudmatchbeta.nvidiagrid.net"));
    assert(route.redirects == 0 && route.actual_host == route.initial_host);
    assert(ShortHost("") == "--");
    assert(ShortHost("very-long-partner-hostname.example.com").size() == 24);
    assert(ShortHost("https://np-waw-01.cloudmatchbeta.nvidiagrid.net/path") == "np-waw-01");
}
