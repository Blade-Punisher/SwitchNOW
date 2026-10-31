#include "diagnostic_file.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main() {
    using namespace opennow::diagnostics;
    const std::string root = "build/host-tests/snapshot-fixture";
    std::filesystem::create_directories(root);
    std::ofstream writer(root + "/buffered.log", std::ios::binary | std::ios::trunc);
    writer << "STREAM diag packets=120\n";
    bool closed = false;
    const auto files = CaptureClosedFiles(root, {"buffered.log", "missing.log"}, [&] {
        writer.flush(); writer.close(); closed = true;
    });
    assert(closed && !writer.is_open());
    assert(files[0].status == "ok" && files[0].content.find("packets=120") != std::string::npos);
    assert(files[1].status == "missing" && files[1].error != 0);
    std::ofstream(root + "/empty.log");
    assert(ReadTail(root, "empty.log").status == "empty");
    std::ofstream(root + "/tail.log", std::ios::binary) << "sensitive_partial_prefix\nSTREAM diag frames=12\n";
    const auto tail = ReadTail(root, "tail.log", 30);
    assert(tail.truncated && tail.content == "STREAM diag frames=12\n");
    std::ofstream(root + "/attempt.log") << "ATTEMPT start id=1\n";
    ArchiveAttempts(root, {"attempt.log"});
    std::ofstream(root + "/attempt.log") << "ATTEMPT start id=2\n";
    ArchiveAttempts(root, {"attempt.log"});
    assert(ReadTail(root,"attempt1_attempt.log").content.find("id=2") != std::string::npos);
    assert(ReadTail(root,"attempt2_attempt.log").content.find("id=1") != std::string::npos);
    std::ofstream(root + "/attempt.log") << "ATTEMPT start id=3\n";
    ArchiveAttempts(root, {"attempt.log", "../invalid"});
    assert(ReadTail(root,"attempt2_attempt.log").content.find("id=2") != std::string::npos);
    assert(ReadTail(root,"attempt.log").content.find("id=3") != std::string::npos);
}
