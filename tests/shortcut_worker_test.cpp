#include "shortcut_worker.hpp"
#include <cassert>
#include <future>
#include <string>
#include <stdexcept>
namespace opennow { void LogRuntimeEvent(const std::string&, const std::string&, const std::string&) {} }
int main() {
    using namespace opennow::shortcut;
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    assert(RunTask([&] { started.set_value(); gate.wait(); }));
    started.get_future().wait();
    assert(!RunTask([] {}));
    release.set_value();
    ShutdownWorker();
    assert(RunTask([] { throw std::runtime_error("codec failure"); }));
    ShutdownWorker();
    bool ran = false;
    assert(RunTask([&] { ran = true; }));
    ShutdownWorker();
    assert(ran);
}
