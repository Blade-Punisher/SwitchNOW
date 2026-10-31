#pragma once
#include <functional>

namespace opennow::shortcut
{
// One owned worker: image codecs must not run on Borealis' small task stack.
bool RunTask(std::function<void()> task);
void ShutdownWorker();
}
