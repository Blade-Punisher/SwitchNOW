#include "routing_diagnostics.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
int main() {
    using namespace opennow;
    assert(routing::Host("https://user:private@eu.example.com:443/path?ticket=hidden") == "eu.example.com:443");
    const std::string root = "build/host-tests/routing-fixture";
    std::filesystem::create_directories(root);
    routing::Begin(root, "Auto", "https://user:private@provider.example/path?ticket=hidden");
    StreamRegion a, b;
    a.name = "Frankfurt"; a.url = "https://frk.example/"; a.ping_ms = 35; a.selection_score_ms = 38;
    a.successful_probes = a.attempted_probes = 4;
    b.name = "Warsaw"; b.url = "https://waw.example/"; b.ping_ms = -1;
    routing::Candidates(root, {a, b}, "fresh_measurements", a.url);
    routing::Event(root, "ready", {{"requested_host", routing::Host(a.url)}, {"actual_host", "np-frk-07.example"}});
    std::ifstream in(root + "/server_selection.log");
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    assert(text.find("probe_kind=TCP_handshake_not_media_ping") != std::string::npos);
    assert(text.find("decision=selected") != std::string::npos);
    assert(text.find("decision=probe_failed") != std::string::npos);
    assert(text.find("actual_host=np-frk-07.example") != std::string::npos);
    assert(text.find("public_queue_fresh=0") != std::string::npos);
    assert(text.find("selection_policy=mixed_endpoints_v2") != std::string::npos);
    assert(text.find("auto_eligible=1") != std::string::npos);
    assert(text.find("auto_rank=") != std::string::npos);
    assert(text.find("private") == std::string::npos && text.find("hidden") == std::string::npos);
    routing::Begin(root, "Manual", "https://provider.example/");
    a.url.clear();
    routing::Candidates(root, {a}, "invalid_case", "");
    std::ifstream invalid(root + "/server_selection.log");
    const std::string bad((std::istreambuf_iterator<char>(invalid)), {});
    assert(bad.find("decision=invalid_endpoint") != std::string::npos);
    assert(bad.find("decision=selected") == std::string::npos);
}
