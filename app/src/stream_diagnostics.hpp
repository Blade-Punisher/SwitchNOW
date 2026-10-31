#pragma once
#include <string>
#include <string_view>
#include "diagnostic_file.hpp"

namespace opennow
{

// One runtime gate for expensive stream diagnostics. The persisted setting is
// applied before a stream starts, so render/audio worker threads can query it.
void SetStreamDiagnosticsEnabled(bool enabled);
bool StreamDiagnosticsEnabled();
void SetSensitiveInputLoggingSuppressed(bool suppressed);
bool SensitiveInputLoggingSuppressed();
bool CanLogInputPayload(std::string_view label);
void AppendAudioDiagnostic(std::string line);
std::vector<diagnostics::FileSnapshot> CaptureStreamDiagnosticFiles();

} // namespace opennow
