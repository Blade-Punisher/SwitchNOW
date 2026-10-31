#ifndef SWITCHNOW_UPDATE_TARGET_H
#define SWITCHNOW_UPDATE_TARGET_H
#include <stdbool.h>
#include <string.h>
#define UPDATE_TARGET_CAPACITY 768
static inline bool update_target_valid(const char* path) {
    if (!path) return false;
    const size_t length = strlen(path);
    if (length < 18 || length + 32 >= UPDATE_TARGET_CAPACITY ||
        strncmp(path, "sdmc:/switch/", 13) != 0 ||
        strcmp(path + length - 4, ".nro") != 0) return false;
    for (const unsigned char* p = (const unsigned char*)path; *p; ++p)
        if (*p < 32 || *p == 127 || *p == '"' || *p == '\\') return false;
    if (strstr(path, "/../") || strstr(path, "/./") || strstr(path, "//")) return false;
    const char* name = strrchr(path, '/') + 1;
    return strcmp(name, "SwitchNOWUpdater.nro") != 0;
}
#endif
