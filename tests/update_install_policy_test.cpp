#include "../app/updater/update_install.h"
#include "../app/updater/update_target.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
int moves, fail_move, commits;
bool fail_commit, fail_verified_target;
std::string target, staged, backup;
void write(const std::string& path, const char* value) { std::ofstream(path) << value; }
std::string read(const std::string& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
int move_file(const char* from, const char* to) {
    if (++moves == fail_move) return -1;
    return std::rename(from, to);
}
bool verify(const char* path) {
    return !(fail_verified_target && path == target) && read(path) == "new verified NRO";
}
bool commit() { return !(fail_commit && ++commits == 1); }
bool exists(const char* path) { return std::filesystem::is_regular_file(path); }
void reset() {
    std::remove(target.c_str()); std::remove(staged.c_str()); std::remove(backup.c_str());
    write(target, "original NRO"); write(staged, "new verified NRO");
    moves = fail_move = commits = 0;
    fail_commit = fail_verified_target = false;
}
}
int main() {
    assert(update_target_valid("sdmc:/switch/SwitchNOW.nro"));
    assert(update_target_valid("sdmc:/switch/My App.nro"));
    assert(update_target_valid("sdmc:/switch/SwitchNOW/SwitchNOW.nro"));
    assert(!update_target_valid("sdmc:/switch/../other.nro"));
    assert(!update_target_valid("sdmc:/switch/SwitchNOWUpdater.nro"));
    assert(!update_target_valid("sdmc:/switch/My\" App.nro"));
    assert(!update_target_valid("sdmc:/switch/file.nro\n"));
    assert(!update_target_valid("romfs:/switch/App.nro"));
    const auto dir = std::filesystem::temp_directory_path() /
        ("switchnow-update-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    target = (dir / "Renamed SwitchNOW.nro").string(); staged = target + ".update"; backup = target + ".previous";
    const UpdateFileOps ops {move_file, std::remove, verify, commit, exists};
    reset();
    assert(update_install(target.c_str(), staged.c_str(), backup.c_str(), &ops) == UPDATE_INSTALLED);
    assert(read(target) == "new verified NRO" && read(backup) == "original NRO");
    assert(!exists(staged.c_str()));
    reset(); fail_move = 2;
    assert(update_install(target.c_str(), staged.c_str(), backup.c_str(), &ops) == UPDATE_ROLLED_BACK);
    assert(read(target) == "original NRO" && read(staged) == "new verified NRO");
    reset(); fail_verified_target = true;
    assert(update_install(target.c_str(), staged.c_str(), backup.c_str(), &ops) == UPDATE_ROLLED_BACK);
    assert(read(target) == "original NRO");
    reset(); fail_commit = true;
    assert(update_install(target.c_str(), staged.c_str(), backup.c_str(), &ops) == UPDATE_ROLLED_BACK);
    assert(read(target) == "original NRO");
    reset(); write(staged, "bad download");
    assert(update_install(target.c_str(), staged.c_str(), backup.c_str(), &ops) == UPDATE_INVALID);
    assert(read(target) == "original NRO" && moves == 0);
    reset(); std::rename(target.c_str(), backup.c_str());
    assert(update_install(target.c_str(), staged.c_str(), backup.c_str(), &ops) == UPDATE_BACKUP_FAILED);
    assert(read(backup) == "original NRO");
    reset(); fail_move = 1;
    assert(update_install(target.c_str(), staged.c_str(), backup.c_str(), &ops) == UPDATE_BACKUP_FAILED);
    assert(read(target) == "original NRO");
    reset(); fail_verified_target = true; fail_move = 3;
    assert(update_install(target.c_str(), staged.c_str(), backup.c_str(), &ops) == UPDATE_ROLLBACK_FAILED);
    assert(read(backup) == "original NRO" && !exists(target.c_str()));
    std::filesystem::remove_all(dir);
}
