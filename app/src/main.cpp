#include "ui_localization.hpp"
#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif

#include <curl/curl.h>

#include <borealis.hpp>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "main_activity.hpp"
#include "app_version.hpp"
#include "connection_debug.hpp"
#include "app_paths.hpp"
#include "cover_image_cache.hpp"
#include "home_shortcut.hpp"
#include "shortcut_worker.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"
#include "stream_diagnostics.hpp"
#include "stream_settings.hpp"
#include "proxy_diagnostics.hpp"
#include "ui_helpers.hpp"
#include "update_service.hpp"

#ifdef __SWITCH__
namespace
{

void EnsureLogDirectory()
{
    opennow::PrepareAppStorage();
}

void AppendBootLog(const std::string& line)
{
    EnsureLogDirectory();

    std::ofstream stream(opennow::AppHomePath() + "/boot.log", std::ios::app);
    if (!stream.is_open())
        return;

    stream << line << '\n';
}

void ShowStartupFailure(const std::string& message)
{
    AppendBootLog("startup failure: " + message);

    consoleInit(nullptr);
    consoleClear();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    printf("SwitchNOW failed to start.\n\n");
    printf("%s\n\n", message.c_str());
    printf("Diagnostics were written to:\n");
    printf("sdmc:/switch/SwitchNOW/boot.log\n");
    printf("sdmc:/switch/SwitchNOW/runtime.log\n\n");
    printf("Press PLUS or B to exit.\n");

    while (appletMainLoop())
    {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        if (down & (HidNpadButton_Plus | HidNpadButton_B))
            break;

        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
}

} // namespace
#endif

int main(int argc, char* argv[])
{
    opennow::PrepareAppStorage();
    if (argc > 0 && argv && argv[0]) {
        opennow::shortcut::SetExecutablePath(argv[0]);
        opennow::update::SetExecutablePath(argv[0]);
    }
    auto launch_request = opennow::shortcut::ReadLaunchRequest(argc, argv);
    auto installer_result = opennow::shortcut::ConsumeInstallerResult();
    opennow::InitializeRuntimeJournal(opennow::kAppVersion);
    opennow::proxy_diagnostics::Configuration(opennow::LoadStreamSettings());
    opennow::connection_debug::Initialize(opennow::AppHomePath(),opennow::kAppVersion);
    const auto shortcut_repair =
        opennow::shortcut::RepairLegacyShortcutManifests();
    opennow::LogRuntimeEvent(
        "shortcut", "manifest_repair",
        "scanned=" + std::to_string(shortcut_repair.scanned) +
            " repaired=" + std::to_string(shortcut_repair.repaired) +
            " failed=" + std::to_string(shortcut_repair.failed));
    AppendBootLog("boot: entered main()");

#ifdef __SWITCH__
    appletSetWirelessPriorityMode(AppletWirelessPriorityMode_OptimizedForWlan);
#endif

    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

    try
    {
        AppendBootLog("boot: calling curl_global_init()");

        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        {
            ShowStartupFailure("curl_global_init failed");
            return EXIT_FAILURE;
        }

        AppendBootLog("boot: curl_global_init ok");
        opennow::connection_debug::Event(0,"curl_initialized");
        if (const curl_version_info_data* version =
                curl_version_info(CURLVERSION_NOW))
        {
            opennow::connection_debug::Event(0,"curl_backend",{{"curl",version->version ? version->version : "unknown"},
                {"tls",version->ssl_version ? version->ssl_version : "unknown"}});
            opennow::LogRuntimeEvent(
                "tls", "backend",
                "curl=" +
                    std::string(version->version ? version->version : "unknown") +
                    " ssl=" +
                    std::string(
                        version->ssl_version ? version->ssl_version : "unknown"));
        }
        AppendBootLog("boot: calling brls::Application::init()");

        if (!brls::Application::init())
        {
            ShowStartupFailure("brls::Application::init returned false");
            curl_global_cleanup();
            return EXIT_FAILURE;
        }

        AppendBootLog("boot: brls::Application::init ok");
        AppendBootLog("boot: creating window");

        const opennow::StreamSettings startup_settings = opennow::LoadStreamSettings();
        opennow::SetInterfaceLanguage(startup_settings.interface_language);
        brls::Application::createWindow("SwitchNOW");
        AppendBootLog("boot: window created");

        // Plus is an in-game Xbox Start/Guide input. Borealis otherwise binds
        // BUTTON_START globally to Application::quit() before StreamView sees it.
        brls::Application::setGlobalQuit(false);
        brls::Application::setFPSStatus(false);
        opennow::SetStreamDiagnosticsEnabled(
            startup_settings.debug_diagnostics);
        opennow::LogRuntimeEvent(
            "diagnostics", "mode",
            startup_settings.debug_diagnostics
                ? "extended=enabled runtime=always_on"
                : "extended=disabled runtime=always_on");

        brls::Application::pushActivity(new opennow::MainActivity(
            std::move(launch_request), std::move(installer_result)),
            brls::TransitionAnimation::FADE);
        opennow::update::ConfirmInstalledUpdate();
        while (brls::Application::mainLoop())
        {
            opennow::LocalizeActionHints();
        }

        AppendBootLog("boot: main loop exited");
        opennow::shortcut::ShutdownWorker();
        opennow::ShutdownImageCache();
        opennow::ShutdownRuntimeJournal();
        curl_global_cleanup();
        opennow::connection_debug::Shutdown();
        return EXIT_SUCCESS;
    }
    catch (const std::exception& ex)
    {
        opennow::LogRuntimeEvent("app", "unhandled_exception", ex.what());
        ShowStartupFailure(std::string("Unhandled exception: ") + ex.what());
    }
    catch (...)
    {
        opennow::LogRuntimeEvent("app", "unhandled_exception", "unknown");
        ShowStartupFailure("Unhandled non-standard exception");
    }

    opennow::shortcut::ShutdownWorker();
    opennow::ShutdownImageCache();
    opennow::ShutdownRuntimeJournal();
    curl_global_cleanup();
    opennow::connection_debug::Shutdown();
    return EXIT_FAILURE;
}
