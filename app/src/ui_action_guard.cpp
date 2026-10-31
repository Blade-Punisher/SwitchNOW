#include "localization.hpp"
#include "ui_action_guard.hpp"

#include "runtime_journal.hpp"
#include "ui_helpers.hpp"
#include "stream_diagnostics.hpp"

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

#include <chrono>
#include <exception>
#include <fstream>
#include <mutex>

namespace opennow
{
namespace
{

std::mutex g_ui_log_mutex;

void EnsureUiLogDirectory()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SwitchNOW", 0777);
#endif
}

} // namespace

void LogUiAction(const std::string& action, const std::string& phase,
                 const std::string& detail)
{
    if (!StreamDiagnosticsEnabled())
        return;
    std::lock_guard<std::mutex> lock(g_ui_log_mutex);
    EnsureUiLogDirectory();
    std::ofstream stream("sdmc:/switch/SwitchNOW/ui.log", std::ios::app);
    if (!stream.is_open())
        return;

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    stream << milliseconds << " action=" << action << " phase=" << phase;
    if (!detail.empty())
        stream << " detail=" << detail;
    stream << '\n';
}

bool RunUiAction(const std::string& action, const std::function<void()>& callback)
{
    const std::uint64_t operation_id =
        BeginRuntimeOperation("ui", action);
    LogUiAction(action, "begin");
    try
    {
        callback();
        LogUiAction(action, "ok");
        EndRuntimeOperation(operation_id, "ui", action, "ok");
        return true;
    }
    catch (const std::exception& exception)
    {
        LogUiAction(action, "error", exception.what());
        EndRuntimeOperation(
            operation_id, "ui", action, "error", exception.what());
        std::string detail =
            action + ": " + exception.what() +
            "\n\nRuntime: sdmc:/switch/SwitchNOW/runtime.log";
        if (StreamDiagnosticsEnabled())
            detail += "\n\nDetails: sdmc:/switch/SwitchNOW/ui.log";
        ShowError(opennow::Tr("Interface Action Failed"), detail);
    }
    catch (...)
    {
        LogUiAction(action, "error", "unknown exception");
        EndRuntimeOperation(
            operation_id, "ui", action, "error", "unknown_exception");
        std::string detail =
            action + ": unknown error"
            "\n\nRuntime: sdmc:/switch/SwitchNOW/runtime.log";
        if (StreamDiagnosticsEnabled())
            detail += "\n\nDetails: sdmc:/switch/SwitchNOW/ui.log";
        ShowError(opennow::Tr("Interface Action Failed"), detail);
    }
    // The action was handled even when its body failed. Returning false would
    // let Borealis dispatch the same button press to another action.
    return true;
}

bool IsViewInside(brls::View* root, brls::View* candidate)
{
    while (candidate)
    {
        if (candidate == root)
            return true;
        if (!candidate->hasParent())
            return false;
        candidate = candidate->getParent();
    }
    return false;
}

void MoveFocusBeforeDestroy(brls::View* root, brls::View* stable_target)
{
    brls::View* focus = brls::Application::getCurrentFocus();
    if (focus && IsViewInside(root, focus))
        brls::Application::giveFocus(stable_target);
}

} // namespace opennow
