#include <mbedtls/sha256.h>
#include <switch.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "update_target.h"
#include "update_install.h"

static char Target[UPDATE_TARGET_CAPACITY];
static char Staged[UPDATE_TARGET_CAPACITY];
static char Marker[UPDATE_TARGET_CAPACITY];
static char Backup[UPDATE_TARGET_CAPACITY];
static const char* LogPath = "sdmc:/switch/SwitchNOW/update.log";

static void write_log(const char* format, ...)
{
    FILE* file = fopen(LogPath, "ab");
    if (!file) return;
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fclose(file);
}

static bool read_expected(char expected[65])
{
    FILE* file = fopen(Marker, "rb");
    if (!file) return false;
    const int matched = fscanf(file, "%64s", expected);
    fclose(file);
    if (matched != 1 || strlen(expected) != 64) return false;
    for (size_t index = 0; index < 64; ++index)
    {
        if (!isxdigit((unsigned char)expected[index])) return false;
        expected[index] = (char)tolower((unsigned char)expected[index]);
    }
    return true;
}

static bool checksum_file(const char* path, char result[65])
{
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (mbedtls_sha256_starts(&context, 0) != 0)
    {
        fclose(file);
        return false;
    }
    static unsigned char buffer[16 * 1024];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0)
        if (mbedtls_sha256_update(&context, buffer, count) != 0)
        {
            fclose(file);
            mbedtls_sha256_free(&context);
            return false;
        }
    const bool read_ok = !ferror(file);
    fclose(file);
    unsigned char digest[32];
    const bool finish_ok = mbedtls_sha256_finish(&context, digest) == 0;
    mbedtls_sha256_free(&context);
    if (!read_ok || !finish_ok) return false;
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0; index < sizeof(digest); ++index)
    {
        result[index * 2] = digits[digest[index] >> 4];
        result[index * 2 + 1] = digits[digest[index] & 15];
    }
    result[64] = '\0';
    return true;
}

static bool verified(const char* path)
{
    char expected[65] = {0};
    char actual[65] = {0};
    return read_expected(expected) && checksum_file(path, actual) && strcmp(expected, actual) == 0;
}

static bool commit_sd(void) { return R_SUCCEEDED(fsdevCommitDevice("sdmc")); }
static bool exists_file(const char* path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

int main(int argc, char** argv)
{
    const bool requested = argc == 3 && argv[1] && strcmp(argv[1], "--finish-update") == 0 &&
        update_target_valid(argv[2]);
    write_log("[helper] start requested=%d\n", requested ? 1 : 0);
    if (!requested) return 0;
    snprintf(Target, sizeof(Target), "%s", argv[2]);
    // Derive sidecar paths from the validated source rather than independently
    // accepting a target/stage pair from the command line.
    snprintf(Staged, sizeof(Staged), "%s.update", argv[2]);
    snprintf(Marker, sizeof(Marker), "%s.update.sha256", argv[2]);
    snprintf(Backup, sizeof(Backup), "%s.previous", argv[2]);
    write_log("[helper] target=%s\n", Target);
    const UpdateFileOps ops = {rename, unlink, verified, commit_sd, exists_file};
    const UpdateInstallResult result = update_install(Target, Staged, Backup, &ops);
    if (result != UPDATE_INSTALLED)
    {
        write_log("[helper] install failed result=%d errno=%d backup=%s\n", result, errno, Backup);
        return 0;
    }
    write_log("[helper] update installed; restarting %s\n", Target);
    char arguments[UPDATE_TARGET_CAPACITY + 2];
    snprintf(arguments, sizeof(arguments), "\"%s\"", Target);
    envSetNextLoad(Target, arguments);
    return 0;
}
