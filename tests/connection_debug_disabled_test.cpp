#include "connection_debug.hpp"
#include <cassert>
#include <filesystem>
int main() {
    using namespace opennow::connection_debug;
    const std::string root="build/host-tests/connection-debug-disabled-fixture";
    std::filesystem::create_directories(root);
    Initialize(root,"normal"); assert(!Enabled());
    Request request("GET","https://host.example/?secret=value",false);
    assert(!request.active()); Event(0,"test"); Shutdown();
    assert(!std::filesystem::exists(root+"/connection_debug.log"));
}
