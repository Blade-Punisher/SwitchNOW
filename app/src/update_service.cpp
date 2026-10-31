#include "update_service.hpp"

#include "app_version.hpp"
#include "app_paths.hpp"
#include "runtime_journal.hpp"
#include "update_policy.hpp"
#include "../updater/update_target.h"

#include <borealis.hpp>
#include <curl/curl.h>
#include <jansson.h>
#include <mbedtls/sha256.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace opennow::update
{
namespace
{

constexpr const char* kLatestReleaseUrl =
    "https://api.github.com/repos/Blade-Punisher/SwitchNOW/releases/latest";
constexpr const char* kHelperRomfsPath = "romfs:/updater/SwitchNOWUpdater.nro";
constexpr const char* kHelperSdPath = "sdmc:/switch/SwitchNOW/SwitchNOWUpdater.nro";
constexpr std::size_t kMetadataLimit = 512 * 1024;
constexpr std::size_t kNroLimit = 64 * 1024 * 1024;

std::string executable_path;
std::string StagedPath() { return executable_path + ".update"; }
std::string MarkerPath() { return StagedPath() + ".sha256"; }
std::string BackupPath() { return executable_path + ".previous"; }

struct MemoryBuffer
{
    std::string data;
    std::size_t limit = 0;
    bool overflow = false;
};

std::size_t WriteMemory(char* data, std::size_t size, std::size_t count, void* opaque)
{
    auto* buffer = static_cast<MemoryBuffer*>(opaque);
    const std::size_t received = size * count;
    if (buffer->data.size() + received > buffer->limit)
    {
        buffer->overflow = true;
        return 0;
    }
    buffer->data.append(data, received);
    return received;
}

struct FileBuffer
{
    std::ofstream stream;
    std::uint64_t written = 0;
};

std::size_t WriteFile(char* data, std::size_t size, std::size_t count, void* opaque)
{
    auto* output = static_cast<FileBuffer*>(opaque);
    const std::size_t received = size * count;
    if (received > kNroLimit || output->written > kNroLimit - received)
        return 0;
    output->stream.write(data, static_cast<std::streamsize>(received));
    if (!output->stream)
        return 0;
    output->written += received;
    return received;
}

struct ProgressState { const ProgressCallback* callback = nullptr; };

int TransferProgress(void* opaque, curl_off_t total, curl_off_t current, curl_off_t, curl_off_t)
{
    const auto* state = static_cast<const ProgressState*>(opaque);
    if (state && state->callback && *state->callback && total > 0)
        (*state->callback)(static_cast<std::uint64_t>(current), static_cast<std::uint64_t>(total));
    return 0;
}

void ConfigureCurl(CURL* curl, const std::string& url)
{
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    const std::string user_agent = std::string("SwitchNOW-Updater/") + kAppVersion;
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}

bool FetchText(const std::string& url, std::size_t limit, std::string& body, std::string& error)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error = "Could not initialize the update network client.";
        return false;
    }
    MemoryBuffer buffer;
    buffer.limit = limit;
    ConfigureCurl(curl, url);
    curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    const CURLcode transfer = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (buffer.overflow)
        error = "The update response was too large.";
    else if (transfer != CURLE_OK)
        error = std::string("Update network error: ") + curl_easy_strerror(transfer);
    else if (status < 200 || status >= 300)
        error = "GitHub returned HTTP " + std::to_string(status) + ".";
    else
    {
        body = std::move(buffer.data);
        return true;
    }
    return false;
}

bool DownloadFile(const std::string& url, const std::string& path,
    const ProgressCallback& progress, std::uint64_t& written, std::string& error)
{
    FileBuffer output;
    output.stream.open(path, std::ios::binary | std::ios::trunc);
    if (!output.stream)
    {
        error = "Could not create the staged update on the SD card.";
        return false;
    }
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error = "Could not initialize the update network client.";
        return false;
    }
    ConfigureCurl(curl, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    ProgressState state {&progress};
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, TransferProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    const CURLcode transfer = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    output.stream.close();
    written = output.written;
    if (transfer != CURLE_OK)
        error = std::string("Update download failed: ") + curl_easy_strerror(transfer);
    else if (status < 200 || status >= 300)
        error = "Update download returned HTTP " + std::to_string(status) + ".";
    else if (written == 0 || written > kNroLimit)
        error = "The downloaded NRO has an invalid size.";
    else
        return true;
    std::remove(path.c_str());
    return false;
}

bool CopyFile(const std::string& source, const std::string& destination, std::string& error)
{
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!input || !output)
    {
        error = "Could not prepare the update helper.";
        return false;
    }
    std::array<char, 64 * 1024> buffer {};
    while (input)
    {
        input.read(buffer.data(), buffer.size());
        const std::streamsize count = input.gcount();
        if (count > 0)
            output.write(buffer.data(), count);
    }
    output.flush();
    if (input.bad() || !output)
    {
        error = "Could not copy the update helper to the SD card.";
        return false;
    }
    return true;
}

bool Sha256File(const std::string& path, std::string& checksum, std::string& error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Could not read the downloaded update.";
        return false;
    }
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (mbedtls_sha256_starts(&context, 0) != 0)
    {
        mbedtls_sha256_free(&context);
        error = "Could not initialize SHA-256 verification.";
        return false;
    }
    std::array<unsigned char, 64 * 1024> buffer {};
    while (input)
    {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const std::streamsize count = input.gcount();
        if (count > 0 && mbedtls_sha256_update(&context, buffer.data(), static_cast<std::size_t>(count)) != 0)
        {
            mbedtls_sha256_free(&context);
            error = "Could not calculate the update checksum.";
            return false;
        }
    }
    std::array<unsigned char, 32> digest {};
    const int finish = mbedtls_sha256_finish(&context, digest.data());
    mbedtls_sha256_free(&context);
    if (input.bad() || finish != 0)
    {
        error = "Could not calculate the update checksum.";
        return false;
    }
    static constexpr char digits[] = "0123456789abcdef";
    checksum.clear();
    checksum.reserve(64);
    for (const unsigned char byte : digest)
    {
        checksum.push_back(digits[byte >> 4]);
        checksum.push_back(digits[byte & 15]);
    }
    return true;
}

bool IsNro(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    input.seekg(0x10);
    std::array<char, 4> magic {};
    input.read(magic.data(), magic.size());
    return input && magic == std::array<char, 4> {'N', 'R', 'O', '0'};
}

std::string NormalizeChecksum(std::string value)
{
    if (value.rfind("sha256:", 0) == 0)
        value.erase(0, 7);
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (value.size() != 64 || !std::all_of(value.begin(), value.end(),
        [](unsigned char character) { return std::isxdigit(character); }))
        return {};
    return value;
}

bool ParseChecksumList(const std::string& body, const std::string& asset, std::string& checksum)
{
    std::istringstream input(body);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.find(asset) == std::string::npos)
            continue;
        std::istringstream fields(line);
        std::string token;
        fields >> token;
        checksum = NormalizeChecksum(token);
        return !checksum.empty();
    }
    return false;
}

} // namespace

void SetExecutablePath(const std::string& path) {
    std::string normalized = path;
    if (normalized.rfind("/switch/", 0) == 0) normalized = "sdmc:" + normalized;
    executable_path = update_target_valid(normalized.c_str()) ? normalized : std::string{};
}

const std::string& ExecutablePath() { return executable_path; }

CheckResult CheckLatestRelease(const std::string& current_version)
{
    CheckResult result;
    std::string body;
    if (!FetchText(kLatestReleaseUrl, kMetadataLimit, body, result.error))
        return result;
    json_error_t parse_error {};
    json_t* root = json_loadb(body.data(), body.size(), JSON_REJECT_DUPLICATES, &parse_error);
    if (!root || !json_is_object(root))
    {
        if (root) json_decref(root);
        result.error = "GitHub returned invalid release metadata.";
        return result;
    }
    json_t* tag = json_object_get(root, "tag_name");
    if (!json_is_string(tag))
    {
        json_decref(root);
        result.error = "The latest release has no version tag.";
        return result;
    }
    result.release.version = json_string_value(tag);
    Version current, candidate;
    if (!ParseVersion(current_version, current) || !ParseVersion(result.release.version, candidate)) {
        json_decref(root);
        result.error = "Could not compare the installed version with the release version.";
        return result;
    }
    result.update_available = IsNewerVersion(result.release.version, current_version);
    if (!result.update_available)
    {
        json_decref(root);
        result.ok = true;
        return result;
    }
    std::string normalized_version = result.release.version;
    if (!normalized_version.empty() &&
        (normalized_version.front() == 'v' || normalized_version.front() == 'V'))
    {
        normalized_version.erase(normalized_version.begin());
    }
    const std::string preferred_asset = "SwitchNOW-" + normalized_version + ".nro";
    ReleaseInfo fallback_release;
    fallback_release.version = result.release.version;
    std::string checksum_url;
    json_t* assets = json_object_get(root, "assets");
    if (json_is_array(assets))
    {
        std::size_t index = 0;
        json_t* asset = nullptr;
        json_array_foreach(assets, index, asset)
        {
            json_t* name_value = json_object_get(asset, "name");
            json_t* url_value = json_object_get(asset, "browser_download_url");
            if (!json_is_string(name_value) || !json_is_string(url_value))
                continue;
            const std::string name = json_string_value(name_value);
            const std::string url = json_string_value(url_value);
            if (!IsTrustedReleaseAsset(url))
                continue;
            if (name == "SHA256SUMS.txt")
                checksum_url = url;
            if (name.rfind("SwitchNOW-", 0) != 0 || name.size() < 5 || name.substr(name.size() - 4) != ".nro")
                continue;
            ReleaseInfo candidate;
            candidate.version = result.release.version;
            candidate.asset_name = name;
            candidate.download_url = url;
            if (json_t* size = json_object_get(asset, "size"); json_is_integer(size))
                candidate.size = static_cast<std::uint64_t>(json_integer_value(size));
            if (json_t* digest = json_object_get(asset, "digest"); json_is_string(digest))
                candidate.sha256 = NormalizeChecksum(json_string_value(digest));
            if (name == preferred_asset)
                result.release = std::move(candidate);
            else if (fallback_release.download_url.empty() &&
                     name.find("test") == std::string::npos &&
                     name.find("debug") == std::string::npos)
                fallback_release = std::move(candidate);
        }
    }
    if (result.release.download_url.empty())
        result.release = std::move(fallback_release);
    result.release.checksum_url = std::move(checksum_url);
    json_decref(root);
    if (result.release.download_url.empty())
    {
        result.error = "The latest GitHub release does not contain a SwitchNOW NRO.";
        return result;
    }
    if (result.release.sha256.empty() && result.release.checksum_url.empty())
    {
        result.error = "The GitHub release does not provide a SHA-256 checksum.";
        return result;
    }
    result.ok = true;
    return result;
}

bool StageRelease(const ReleaseInfo& release, const ProgressCallback& progress, std::string& error)
{
    if (executable_path.empty() || !IsNro(executable_path)) {
        error = "Could not locate the running NRO. Launch SwitchNOW from its NRO in the switch folder.";
        return false;
    }
    if (!IsTrustedReleaseAsset(release.download_url))
    {
        error = "The update asset URL is not trusted.";
        return false;
    }
    PrepareAppStorage();
    const std::string staged = StagedPath();
    const std::string marker = MarkerPath();
    std::remove(staged.c_str());
    std::remove(marker.c_str());
    std::remove(kHelperSdPath);

    std::string expected = release.sha256;
    if (expected.empty())
    {
        if (!IsTrustedReleaseAsset(release.checksum_url)) {
            error = "The update checksum URL is not trusted.";
            return false;
        }
        std::string checksums;
        if (!FetchText(release.checksum_url, 64 * 1024, checksums, error) ||
            !ParseChecksumList(checksums, release.asset_name, expected))
        {
            if (error.empty()) error = "Could not find the NRO checksum in SHA256SUMS.txt.";
            return false;
        }
    }
    std::uint64_t written = 0;
    if (!DownloadFile(release.download_url, staged, progress, written, error))
        return false;
    if ((release.size > 0 && written != release.size) || !IsNro(staged))
    {
        std::remove(staged.c_str());
        error = "The downloaded file is not the expected SwitchNOW NRO.";
        return false;
    }
    std::string actual;
    if (!Sha256File(staged, actual, error) || actual != expected)
    {
        std::remove(staged.c_str());
        if (error.empty()) error = "The downloaded update failed SHA-256 verification.";
        return false;
    }
    std::ofstream marker_file(marker, std::ios::binary | std::ios::trunc);
    marker_file << expected << '\n';
    marker_file.close();
    if (!marker_file || !CopyFile(kHelperRomfsPath, kHelperSdPath, error))
    {
        std::remove(staged.c_str());
        std::remove(marker.c_str());
        return false;
    }
#ifdef __SWITCH__
    if (R_FAILED(fsdevCommitDevice("sdmc")))
    {
        error = "Could not commit the staged update to the SD card.";
        return false;
    }
#endif
    LogRuntimeEvent("update", "staged", "version=" + release.version + " target=" + executable_path + " bytes=" + std::to_string(written));
    return true;
}

bool LaunchUpdateHelper(std::string& error)
{
#ifdef __SWITCH__
    struct stat info {};
    if (stat(kHelperSdPath, &info) != 0 || stat(StagedPath().c_str(), &info) != 0)
    {
        error = "The staged update or update helper is missing.";
        return false;
    }
    if (!update_target_valid(executable_path.c_str())) {
        error = "The running NRO path is unavailable.";
        return false;
    }
    const std::string arguments = std::string("\"") + kHelperSdPath +
        "\" --finish-update \"" + executable_path + "\"";
    envSetNextLoad(kHelperSdPath, arguments.c_str());
    LogRuntimeEvent("update", "handoff", "target=" + executable_path);
    brls::Application::quit();
    return true;
#else
    error = "Updates can only be installed on Nintendo Switch.";
    return false;
#endif
}

void ConfirmInstalledUpdate()
{
    if (executable_path.empty()) return;
    struct stat info {};
    if (stat(BackupPath().c_str(), &info) != 0)
        return;
    std::ifstream marker(MarkerPath());
    std::string expected, actual, error;
    marker >> expected;
    marker.close();
    expected = NormalizeChecksum(expected);
    if (expected.empty() || !Sha256File(executable_path, actual, error) || actual != expected) {
        LogRuntimeEvent("update", "confirmation_failed", "backup retained target=" + executable_path);
        return;
    }
    std::remove(BackupPath().c_str());
    std::remove(MarkerPath().c_str());
    std::remove(kHelperSdPath);
#ifdef __SWITCH__
    fsdevCommitDevice("sdmc");
#endif
    LogRuntimeEvent("update", "confirmed", "current NRO started successfully");
}

} // namespace opennow::update
