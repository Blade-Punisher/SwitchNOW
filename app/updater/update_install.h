#ifndef SWITCHNOW_UPDATE_INSTALL_H
#define SWITCHNOW_UPDATE_INSTALL_H
#include <stdbool.h>
typedef struct UpdateFileOps {
    int (*move)(const char*, const char*);
    int (*remove)(const char*);
    bool (*verify)(const char*);
    bool (*commit)(void);
    bool (*exists)(const char*);
} UpdateFileOps;
typedef enum UpdateInstallResult {
    UPDATE_INSTALLED, UPDATE_INVALID, UPDATE_BACKUP_FAILED,
    UPDATE_ROLLED_BACK, UPDATE_ROLLBACK_FAILED
} UpdateInstallResult;
static inline UpdateInstallResult update_install(const char* target, const char* staged,
    const char* backup, const UpdateFileOps* ops) {
    if (!ops->verify(staged)) return UPDATE_INVALID;
    if (!ops->exists(target)) return UPDATE_BACKUP_FAILED;
    ops->remove(backup);
    if (ops->move(target, backup) != 0) return UPDATE_BACKUP_FAILED;
    const bool moved = ops->move(staged, target) == 0;
    if (moved && ops->verify(target) && ops->commit()) return UPDATE_INSTALLED;
    // A failed staging rename may leave a different file at target: never
    // unlink it unless we successfully installed our own file there.
    if (moved && ops->remove(target) != 0) return UPDATE_ROLLBACK_FAILED;
    if (ops->move(backup, target) != 0 || !ops->commit()) return UPDATE_ROLLBACK_FAILED;
    return UPDATE_ROLLED_BACK;
}
#endif
