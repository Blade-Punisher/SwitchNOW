#pragma once

#include <string>

namespace opennow::session_error
{

struct Presentation
{
    std::string title;
    std::string body;
};

inline bool Contains(const std::string& value, const char* needle)
{
    return value.find(needle) != std::string::npos;
}

inline Presentation Present(const std::string& error)
{
    if (Contains(error, "HTTP 429") || Contains(error, "REQUEST_LIMIT_EXCEEDED"))
    {
        return {
            "Too many launch attempts",
            "GeForce NOW temporarily limited launch requests. Wait a few minutes before trying again. "
            "Restarting the application does not reset this limit.",
        };
    }

    if (Contains(error, "SESSION_LIMIT_PER_DEVICE_EXCEEDED") ||
        Contains(error, "statusCode=50"))
    {
        return {
            "A session is already active",
            "GeForce NOW still has a session attached to this device. End the other session, "
            "wait a moment, and try again.",
        };
    }

    if (Contains(error, "SESSION_LIMIT") || Contains(error, "statusCode=51"))
    {
        return {
            "Your account is already streaming",
            "End the active GeForce NOW session on the other device, then try again.",
        };
    }

    if (Contains(error, "statusCode=81") || Contains(error, "membership"))
    {
        return {
            "Membership required",
            "This game is not included with the current GeForce NOW membership.",
        };
    }

    if (Contains(error, "HTTP 401") || Contains(error, "sign in again") ||
        Contains(error, "login is no longer valid"))
    {
        return {
            "Sign in again",
            "Your NVIDIA authorization expired. Open Library and sign in to this account again.",
        };
    }

    std::string concise = error;
    const size_t diagnostics = concise.find("\nEnable Settings");
    if (diagnostics != std::string::npos)
        concise.erase(diagnostics);
    const size_t details = concise.find("\nDetails:");
    if (details != std::string::npos)
        concise.erase(details);
    if (concise.size() > 280)
        concise = concise.substr(0, 277) + "...";

    return {
        "Session could not start",
        concise.empty() ? "GeForce NOW did not start the session. Please try again." : concise,
    };
}

} // namespace opennow::session_error
