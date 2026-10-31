#pragma once
#include "connection_debug_curl.hpp"
#include "network_resource_policy.hpp"

namespace opennow::network {
struct SocketGuard {
    connection_debug::Request& trace;
    bool probe = false;
    unsigned attempts = 0, opened = 0;
    int error = 0;
};
inline curl_socket_t GuardOpen(void* opaque, curlsocktype kind, curl_sockaddr* address) noexcept {
    auto& state = *static_cast<SocketGuard*>(opaque);
    // One address per optional probe: curl's address fallback must not turn one
    // budgeted measurement into a burst of socket creations.
    if (state.probe && (state.attempts != 0 || !ProbeAllowed())) {
        errno = ECANCELED;
        return CURL_SOCKET_BAD;
    }
    ++state.attempts;
    const auto fd = connection_debug::OpenSocket(&state.trace, kind, address);
    const int saved = errno;
    if (fd == CURL_SOCKET_BAD) {
#ifdef _WIN32
        state.error = WSAGetLastError() == WSAENOBUFS ? ENOBUFS : saved;
#else
        state.error = saved;
#endif
        ResourceFailure(state.error);
    } else ++state.opened;
    errno = saved;
    return fd;
}
inline int GuardClose(void* opaque, curl_socket_t fd) noexcept {
    return connection_debug::CloseSocket(&static_cast<SocketGuard*>(opaque)->trace, fd);
}
inline void AttachGuard(CURL* curl, SocketGuard& guard) {
    connection_debug::Attach(curl, guard.trace);
    // Resource capture is necessary even in a normal build with debug disabled.
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, GuardOpen);
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, &guard);
    curl_easy_setopt(curl, CURLOPT_CLOSESOCKETFUNCTION, GuardClose);
    curl_easy_setopt(curl, CURLOPT_CLOSESOCKETDATA, &guard);
}
}
