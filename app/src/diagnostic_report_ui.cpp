#include "diagnostic_report.hpp"
#include "ui_helpers.hpp"
#include "app_paths.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"
#include "stream_settings.hpp"
#include "stream_diagnostics.hpp"
#include <borealis.hpp>
#include <atomic>
#include <memory>

namespace opennow {
void SaveDiagnosticReportDialog() {
    static std::weak_ptr<std::atomic<bool>> active;
    if (const auto job = active.lock(); job && job->load()) return;
    auto running = std::make_shared<std::atomic<bool>>(true);
    active = running;
    const auto settings = LoadStreamSettings();
    // Explicit numeric settings only: never serialize proxy URLs or login data.
    const std::string summary = "Configured stream: " + std::to_string(settings.width) + "x" +
        std::to_string(settings.height) + " @ " + std::to_string(settings.fps) +
        " fps, " + std::to_string(settings.bitrate_kbps) + " kbps\nAudio buffer: " +
        std::to_string(settings.audio_buffer_ms) + " ms\nDiagnostics: " +
        (settings.debug_diagnostics ? "enabled" : "disabled");
    auto* dialog = new brls::Dialog(Tr("Saving diagnostic report..."));
    dialog->setCancelable(false);
    dialog->addButton(Tr("Close"), [running] { running->store(false); });
    dialog->open();
    brls::async([running, dialog, summary] {
        try {
            PrepareAppStorage();
            const auto snapshot = CaptureStreamDiagnosticFiles();
            const auto path = report::Create(AppHomePath(), summary, snapshot);
            LogRuntimeEvent("diagnostics", "report_saved");
            brls::sync([running, dialog, path] {
                if (!running->exchange(false)) return;
                dialog->close();
                ShowDialog(opennow::Tr("Diagnostic report saved"), path);
            });
        } catch (...) {
            brls::sync([running, dialog] {
                if (!running->exchange(false)) return;
                dialog->close();
                ShowError(opennow::Tr("Diagnostic report"), opennow::Tr("Could not save the report. Check free space on the SD card."));
            });
        }
    });
}
} // namespace opennow
