#include "shortcut_worker.hpp"
#include "runtime_journal.hpp"
#include <atomic>
#include <memory>
#include <pthread.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace opennow::shortcut
{
namespace
{
pthread_t worker {};
bool joinable = false; // RunTask and ShutdownWorker are called on the UI thread.
std::atomic_bool busy {false};
void* Execute(void* argument)
{
    std::unique_ptr<std::function<void()>> task(
        static_cast<std::function<void()>*>(argument));
#ifdef __SWITCH__
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3B);
#endif
    try { (*task)(); }
    catch (...) { LogRuntimeEvent("shortcut", "worker_exception"); }
    busy.store(false);
    return nullptr;
}
}
bool RunTask(std::function<void()> task)
{
    if (busy.exchange(true))
        return false;
    if (joinable)
        pthread_join(worker, nullptr);
    joinable = false;
    auto work = std::make_unique<std::function<void()>>(std::move(task));
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
    {
        busy.store(false);
        return false;
    }
    int error = pthread_attr_setstacksize(&attributes, 1024 * 1024);
    if (!error)
        error = pthread_create(&worker, &attributes, Execute, work.get());
    pthread_attr_destroy(&attributes);
    if (error)
    {
        busy.store(false);
        return false;
    }
    work.release();
    joinable = true;
    return true;
}
void ShutdownWorker()
{
    if (joinable)
        pthread_join(worker, nullptr);
    joinable = false;
}
}
