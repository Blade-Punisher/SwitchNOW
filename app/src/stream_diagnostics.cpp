#include "stream_diagnostics.hpp"

#include <atomic>

namespace opennow
{
namespace
{

std::atomic_bool g_stream_diagnostics_enabled {false};
std::atomic_bool g_sensitive_input_logging_suppressed {false};

} // namespace

void SetStreamDiagnosticsEnabled(bool enabled)
{
    g_stream_diagnostics_enabled.store(enabled, std::memory_order_release);
}

bool StreamDiagnosticsEnabled()
{
    return g_stream_diagnostics_enabled.load(std::memory_order_acquire);
}

void SetSensitiveInputLoggingSuppressed(bool suppressed)
{
    g_sensitive_input_logging_suppressed.store(suppressed, std::memory_order_release);
}

bool SensitiveInputLoggingSuppressed()
{
    return g_sensitive_input_logging_suppressed.load(std::memory_order_acquire);
}

bool CanLogInputPayload(std::string_view label)
{
    return !SensitiveInputLoggingSuppressed() || (label != "key_down" && label != "key_up");
}

} // namespace opennow
