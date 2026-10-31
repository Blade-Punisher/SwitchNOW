#include "home_shortcut.hpp"

#include "app_version.hpp"
#include "atomic_file_replace.hpp"
#include "cover_image_cache.hpp"
#include "nro_shortcut_policy.hpp"
#include "gfn_client.hpp"
#include "runtime_journal.hpp"
#include <mbedtls/sha256.h>

#include "shortcut_icon.hpp"
#include "native_home_artwork.hpp"

#ifdef __SWITCH__
#include <dirent.h>
#include <switch.h>
#include <sys/stat.h>
#endif

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <jansson.h>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace opennow::shortcut
{
namespace
{

constexpr const char* kDefaultExecutable =
    "sdmc:/switch/SwitchNOW/SwitchNOW.nro";
constexpr const char* kShortcutTemplate =
    "romfs:/shortcut/SwitchNOWShortcut.nro";
constexpr const char* kDefaultIcon =
    "romfs:/icon/icon.jpg";
constexpr const char* kInstallerRomfsPath =
    "romfs:/shortcut/SwitchNOWForwarderInstaller.nro";
constexpr const char* kInstallerSdPath =
    "sdmc:/switch/SwitchNOW/.shortcut/SwitchNOWForwarderInstaller.nro";
constexpr const char* kInstallerRequestPath =
    "sdmc:/switch/SwitchNOW/forwarder_request.json";
constexpr const char* kInstallerResultPath =
    "sdmc:/switch/SwitchNOW/forwarder_result.json";
constexpr const char* kActiveExecutablePath =
    "sdmc:/switch/SwitchNOW/active_nro_path.txt";
constexpr const char* kShortcutRoot =
    "sdmc:/switch/SwitchNOW Shortcuts";

std::string g_executable_path = kDefaultExecutable;


bool ReadBinaryFile(const std::string& path, std::vector<std::uint8_t>& data)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return false;
    data.assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    return !data.empty();
}

bool WriteBinaryFileAtomically(
    const std::string& path, const std::vector<std::uint8_t>& data)
{
    const std::string temporary = path + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
            return false;
        stream.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream.good())
            return false;
        stream.close();
        if (stream.fail())
            return false;
    }
    return storage::ReplaceWithTemporaryFile(temporary, path);
}

bool WriteTextFileAtomically(const std::string& path, const std::string& data)
{
    return WriteBinaryFileAtomically(
        path, std::vector<std::uint8_t>(data.begin(), data.end()));
}

std::string ReadTextFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return {};
    return {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
}

void EnsureShortcutDirectories(const std::string& game_directory)
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SwitchNOW Shortcuts", 0777);
    mkdir(game_directory.c_str(), 0777);
#else
    (void)game_directory;
#endif
}

std::string JsonEscape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char ch : value)
    {
        switch (ch)
        {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch >= 0x20)
                    escaped.push_back(static_cast<char>(ch));
                break;
        }
    }
    return escaped;
}

std::optional<LaunchRequest> ReadManifestArgument(
    const std::vector<std::string>& arguments)
{
    for (const std::string& argument : arguments)
    {
        if (argument.size() < 10 ||
            argument.substr(argument.size() - 10) != ".switchnow")
            continue;
        const std::string manifest = ReadTextFile(argument);
        if (manifest.empty())
            return std::nullopt;
        auto request = Parse(manifest);
        if (request && argument.rfind(std::string(kShortcutRoot) + "/", 0) == 0 &&
            argument.find("/../") == std::string::npos)
            request->manifest_path = argument;
        return request;
    }
    return std::nullopt;
}

#ifdef __SWITCH__
bool IsRegularFile(const std::string& path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool IsUsableExecutablePath(const std::string& path)
{
    if (path.size() < 11 || path.size() >= FS_MAX_PATH ||
        path.rfind("sdmc:/", 0) != 0 || path.find_first_of("\r\n") != std::string::npos)
        return false;
    const size_t slash = path.find_last_of('/');
    const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    if (name.rfind("SwitchNOW", 0) != 0 || name.size() < 5 ||
        name.substr(name.size() - 4) != ".nro")
        return false;
    return IsRegularFile(path);
}

void RepairManifest(
    const std::string& path, RepairResult& result)
{
    ++result.scanned;
    const std::string document = ReadTextFile(path);
    auto request = Parse(document);
    if (!request)
    {
        ++result.failed;
        return;
    }
    if (request->executable_path == g_executable_path)
        return;
    request->executable_path = g_executable_path;
    if (WriteTextFileAtomically(path, Serialize(*request)))
        ++result.repaired;
    else
        ++result.failed;
}

void RepairManifestDirectory(
    const std::string& directory, RepairResult& result, bool recurse)
{
    DIR* handle = opendir(directory.c_str());
    if (!handle)
        return;
    while (dirent* entry = readdir(handle))
    {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string path = directory + "/" + name;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0)
            continue;
        if (recurse && S_ISDIR(info.st_mode))
        {
            RepairManifestDirectory(path, result, false);
            continue;
        }
        if (!S_ISREG(info.st_mode) || name.size() < 10 ||
            name.substr(name.size() - 10) != ".switchnow")
            continue;
        RepairManifest(path, result);
    }
    closedir(handle);
}
#endif

} // namespace

void SetExecutablePath(std::string path)
{
#ifdef __SWITCH__
    if (!IsUsableExecutablePath(path))
        return;
#else
    if (path.empty() || path.find('\n') != std::string::npos || path.size() > 1024)
        return;
#endif
    g_executable_path = std::move(path);
    WriteTextFileAtomically(kActiveExecutablePath, g_executable_path + "\n");
}

const std::string& ExecutablePath()
{
    return g_executable_path;
}

RepairResult RepairLegacyShortcutManifests()
{
    RepairResult result;
#ifdef __SWITCH__
    if (IsUsableExecutablePath(g_executable_path))
        RepairManifestDirectory(kShortcutRoot, result, true);
#endif
    return result;
}

std::optional<LaunchRequest> ReadLaunchRequest(int argc, char* argv[])
{
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i])
            arguments.emplace_back(argv[i]);
    }
    if (auto manifest = ReadManifestArgument(arguments))
    {
        return manifest;
    }
    return ParseArguments(arguments);
}

std::optional<InstallerResult> ConsumeInstallerResult()
{
    const std::string document = ReadTextFile(kInstallerResultPath);
    if (document.empty())
        return std::nullopt;

    // Consume once even if an interrupted write left malformed JSON. This
    // prevents a stale installer result from appearing on every later boot.
    std::remove(kInstallerResultPath);

    json_error_t json_error {};
    json_t* root = json_loadb(
        document.data(), document.size(), JSON_REJECT_DUPLICATES, &json_error);
    if (!root || !json_is_object(root))
    {
        if (root)
            json_decref(root);
        return std::nullopt;
    }

    if (json_is_true(json_object_get(root, "automatic")))
    {
        LogRuntimeEvent("shortcut", "home_artwork_install",
            json_is_true(json_object_get(root, "success")) ? "success" : "failed");
        json_decref(root);
        return std::nullopt;
    }
    InstallerResult result;
    result.success = json_is_true(json_object_get(root, "success"));
    if (json_t* name = json_object_get(root, "name"); json_is_string(name))
        result.title = json_string_value(name);
    if (json_t* error = json_object_get(root, "error"); json_is_string(error))
        result.error = json_string_value(error);
    json_decref(root);
    return result;
}

CreateResult CreateGameShortcut(LaunchRequest request)
{
    CreateResult result;
    result.title = request.title;
    request.executable_path = g_executable_path;
    if (!IsValid(request))
    {
        result.error = "The selected game does not have valid shortcut metadata.";
        return result;
    }

    const std::string stem = SafeFileStem(request.title, request.launch_app_id);
    const std::string directory =
        "sdmc:/switch/SwitchNOW Shortcuts/" + stem + " [" +
        request.launch_app_id + "]";
    EnsureShortcutDirectories(directory);

    std::vector<std::uint8_t> template_nro;
    if (!ReadBinaryFile(kShortcutTemplate, template_nro))
    {
        result.error = "The SwitchNOW shortcut launcher is missing from RomFS.";
        return result;
    }

    std::vector<std::uint8_t> icon;
    try
    {
        icon = MakeSquareJpeg(LoadCachedImageData(ResizeNvidiaImageUrl(request.image_url, kArtworkSourceWidth)));
        result.used_game_cover = !icon.empty();
    }
    catch (...)
    {
        icon.clear();
    }
    if (icon.empty() && !ReadBinaryFile(kDefaultIcon, icon))
    {
        result.error = "Could not load the SwitchNOW shortcut icon.";
        return result;
    }

    std::vector<std::uint8_t> shortcut_nro;
    if (!BuildShortcutNro(template_nro, icon, request.title, shortcut_nro))
    {
        result.error = "Could not personalize the shortcut launcher.";
        return result;
    }

    result.nro_path = directory + "/" + stem + ".nro";
    result.manifest_path = directory + "/" + stem + ".switchnow";
    if (!WriteBinaryFileAtomically(result.nro_path, shortcut_nro))
    {
        result.error = "Could not write the shortcut NRO to the SD card.";
        return result;
    }
    if (!WriteTextFileAtomically(result.manifest_path, Serialize(request)))
    {
        result.error = "Could not write the shortcut launch manifest.";
        return result;
    }

    result.success = true;
    return result;
}

bool RefreshGameShortcut(LaunchRequest& request, AuthSession session)
{
    if (request.manifest_path.empty())
        return false;
    try
    {
        GfnClient client;
        const auto games = client.FetchShortcutArtwork(session, request.game_id);
        for (const auto& game : games)
        {
            const bool matches = (!request.game_id.empty() &&
                (game.uuid == request.game_id || game.id == request.game_id)) ||
                game.launch_app_id == request.launch_app_id ||
                std::any_of(game.variants.begin(), game.variants.end(),
                    [&](const auto& variant) { return variant.id == request.launch_app_id; });
            if (matches && !game.image_url.empty())
            {
                request.image_url = game.image_url;
                break;
            }
        }
        const std::string nro_path = request.manifest_path.substr(
            0, request.manifest_path.size() - 10) + ".nro";
        std::vector<uint8_t> original;
        if (!ReadBinaryFile(nro_path, original)) return false;
        const auto aset = detail::ReadU32(original, 0x18);
        if (detail::ReadU32(original, aset) != 0x54455341) return false;
        size_t icon_begin=0,icon_size=0;
        if (!detail::CheckedRange(aset,detail::ReadU64(original,aset+8),
                detail::ReadU64(original,aset+16),original.size(),icon_begin,icon_size) ||
            icon_size==0 || icon_size>=0x20000) return false;
        const std::vector<uint8_t> previous_icon(original.begin()+icon_begin,original.begin()+icon_begin+icon_size);
        auto hash_text=[](const auto& bytes) {
            unsigned char digest[32];
            if(mbedtls_sha256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest,0)!=0)
                throw std::runtime_error("Artwork hash failed");
            static const char* hex="0123456789abcdef";std::string out;
            for(auto byte:digest){out+=hex[byte>>4];out+=hex[byte&15];}return out;
        };
        const std::string state_path=request.manifest_path+".artwork-http";
        const auto document=ReadTextFile(state_path);json_error_t error{};
        std::unique_ptr<json_t,decltype(&json_decref)> state(
            json_loadb(document.data(),document.size(),0,&error),json_decref);
        auto field=[&](const char* name) {
            auto* value=state?json_object_get(state.get(),name):nullptr;
            return std::string(json_is_string(value)?json_string_value(value):"");
        };
        const auto url=ResizeNvidiaImageUrl(request.image_url,kArtworkSourceWidth);
        const bool valid_state=field("pipeline")==kArtworkPipeline && field("url")==url && field("icon_hash")==hash_text(previous_icon);
        std::vector<std::string> headers {"Accept: image/jpeg,image/png,image/*", "Cache-Control: no-cache"};
        auto validator=[&](const char* name,const char* header) {
            const auto value=field(name);
            if(valid_state && !value.empty() && value.size()<=1024 && value.find_first_of("\r\n")==std::string::npos)
                headers.push_back(std::string(header)+value);
        };
        validator("etag","If-None-Match: ");validator("modified","If-Modified-Since: ");
        const auto response=HttpClient().Get(url,GfnClient::kUserAgent,headers,2,4,2*1024*1024);
        std::vector<uint8_t> icon;
        std::string pixel_hash;
        if(response.status_code==304 && valid_state)icon=previous_icon;
        else if(response.status_code==200) {
            icon=MakeSquareJpeg(response.body,[&](const auto& pixels){pixel_hash=hash_text(pixels);});
            if(valid_state && !pixel_hash.empty() && pixel_hash==field("pixel_hash"))icon=previous_icon;
        }
        if(icon.empty())return false;
        if(icon!=previous_icon) {
            std::vector<uint8_t> updated;
            if(!BuildShortcutNro(original,icon,request.title,updated) || !WriteBinaryFileAtomically(nro_path,updated))return false;
        }
        const auto manifest=Serialize(request);
        if(ReadTextFile(request.manifest_path)!=manifest)WriteTextFileAtomically(request.manifest_path,manifest);
        if(response.status_code==200) {
            std::unique_ptr<json_t,decltype(&json_decref)> next(json_pack("{s:s,s:s,s:s,s:s,s:s,s:s}",
                "pipeline",kArtworkPipeline,"url",url.c_str(),"etag",response.etag.c_str(),"modified",response.last_modified.c_str(),
                "icon_hash",hash_text(icon).c_str(),"pixel_hash",pixel_hash.c_str()),json_decref);
            if(next) {
                char* text=json_dumps(next.get(),JSON_COMPACT);
                if(text){WriteTextFileAtomically(state_path,text);free(text);}
            }
        }
        LogRuntimeEvent("shortcut","artwork_check",response.status_code==304?"not_modified":"checked");
        return UpdateInstalledHomeIcon(nro_path,icon);
    }
    catch (const std::exception& error)
    {
        LogRuntimeEvent("shortcut", "artwork_refresh_failed", error.what());
    }
    catch (...) { LogRuntimeEvent("shortcut", "artwork_refresh_failed"); }
    return false;
}

bool StartForwarderInstaller(
    const std::string& nro_path, const std::string& title,
    std::string& error, bool automatic)
{
#ifdef __SWITCH__
    if (appletGetAppletType() != AppletType_Application)
    {
        error =
            "Horizon installation requires full-memory/application mode. "
            "Hold R while opening an installed title, then launch SwitchNOW.";
        return false;
    }
    if (nro_path.empty() || nro_path.size() >= FS_MAX_PATH || title.empty() ||
        nro_path.find('\n') != std::string::npos)
    {
        error = "The forwarder target is invalid.";
        return false;
    }

    mkdir("sdmc:/switch/SwitchNOW/.shortcut", 0777);
    std::vector<std::uint8_t> installer;
    if (!ReadBinaryFile(kInstallerRomfsPath, installer))
    {
        error = "The SwitchNOW Forwarder Installer is missing from RomFS.";
        return false;
    }
    if (!WriteBinaryFileAtomically(kInstallerSdPath, installer))
    {
        error = "Could not prepare the SwitchNOW Forwarder Installer on the SD card.";
        return false;
    }

    const std::string arguments = "\"" + nro_path + "\"";
    const std::string request =
        "{\n"
        "  \"version\": 1,\n"
        "  \"name\": \"" + JsonEscape(title) + "\",\n"
        "  \"author\": \"Blade-Punisher\",\n"
        "  \"displayVersion\": \"SwitchNOW\",\n"
        "  \"nroPath\": \"" + JsonEscape(nro_path) + "\",\n"
        "  \"returnPath\": \"" + JsonEscape(g_executable_path) + "\",\n"
        "  \"args\": \"" + JsonEscape(arguments) + "\"\n"
        "}\n";
    std::string install_request = request;
    if (automatic)
    {
        install_request.erase(install_request.rfind('}'));
        install_request += ", \"automatic\": true\n}\n";
    }
    if (!WriteTextFileAtomically(kInstallerRequestPath, install_request))
    {
        error = "Could not write the forwarder installation request.";
        return false;
    }

    envSetNextLoad(kInstallerSdPath, kInstallerSdPath);
    brls::Application::quit();
    return true;
#else
    (void)nro_path;
    (void)title;
    error = "Horizon forwarder installation is only available on Nintendo Switch.";
    return false;
#endif
}

} // namespace opennow::shortcut
