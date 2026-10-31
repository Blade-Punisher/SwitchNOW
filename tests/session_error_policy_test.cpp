#include "session_error_policy.hpp"

#include <cassert>

int main()
{
    using opennow::session_error::Present;

    const auto device_limit = Present(
        "StartSession failed: HTTP 403, statusCode=50, "
        "statusDescription=SESSION_LIMIT_PER_DEVICE_EXCEEDED_STATUS");
    assert(device_limit.title == "A session is already active");
    assert(device_limit.body.find("statusCode") == std::string::npos);

    const auto auth = Present("StartSession failed: HTTP 401");
    assert(auth.title == "Sign in again");

    const auto rate_limit = Present("StartSession failed: HTTP 429, statusCode=10, REQUEST_LIMIT_EXCEEDED_STATUS");
    assert(rate_limit.title == "Too many launch attempts");
    assert(rate_limit.body.find("Wait a few minutes") != std::string::npos);
    assert(Present("REQUEST_LIMIT_EXCEEDED_STATUS").title == rate_limit.title);

    const auto generic = Present(
        "Network request failed\nEnable Settings > Stream > Debug diagnostics");
    assert(generic.title == "Session could not start");
    assert(generic.body == "Network request failed");
    return 0;
}
