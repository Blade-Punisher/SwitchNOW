#pragma once

#include "models.hpp"

#include <string>

namespace opennow::provider_picker
{

inline bool IsNvidia(const LoginProvider& provider)
{
    return provider.code.empty() || provider.code == "NVIDIA";
}

inline std::string DisplayName(const LoginProvider& provider)
{
    if (!provider.display_name.empty())
        return provider.display_name;
    if (!provider.code.empty())
        return provider.code;
    return "GeForce NOW";
}

inline const char* TypeLabel(const LoginProvider& provider)
{
    return IsNvidia(provider) ? "NVIDIA" : "ALLIANCE";
}

inline const char* SignInLabel(const LoginProvider& provider)
{
    return IsNvidia(provider) ? "Fast sign-in on this console"
                              : "Secure sign-in on phone or PC";
}

inline const char* Description(const LoginProvider& provider, bool current)
{
    if (current)
        return "This operator is used by the active account.";
    return IsNvidia(provider)
        ? "Official service with fast sign-in inside SwitchNOW."
        : "Partner service using its own secure authorization page.";
}

inline std::string ProviderCode(const LoginProvider& provider)
{
    return provider.code.empty() ? std::string("NVIDIA") : provider.code;
}

inline bool IsCurrent(
    const LoginProvider& provider, const LoginProvider* current_provider)
{
    if (!current_provider)
        return false;
    if (!provider.idp_id.empty() && !current_provider->idp_id.empty())
        return provider.idp_id == current_provider->idp_id;
    return ProviderCode(provider) == ProviderCode(*current_provider);
}

} // namespace opennow::provider_picker
