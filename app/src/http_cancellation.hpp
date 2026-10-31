#pragma once
#include <functional>
#include <utility>

namespace opennow {
// Scoped to the recovery worker thread, including nested token refresh and
// membership HTTP calls. Normal streaming threads have no cancellation scope.
class HttpCancellationScope {
public:
    explicit HttpCancellationScope(std::function<bool()> cancelled)
        : previous_(std::move(Current())) {
        Current() = [parent = previous_, cancelled = std::move(cancelled)] {
            return (parent && parent()) || (cancelled && cancelled());
        };
    }
    ~HttpCancellationScope() { Current() = std::move(previous_); }
    HttpCancellationScope(const HttpCancellationScope&) = delete;
    HttpCancellationScope& operator=(const HttpCancellationScope&) = delete;
    static std::function<bool()> Callback() { return Current(); }
private:
    // A function-local inline TLS object is shared across translation units,
    // including MinGW host tests (which otherwise duplicate the TLS init thunk).
    static std::function<bool()>& Current() {
        static thread_local std::function<bool()> current;
        return current;
    }
    std::function<bool()> previous_;
};
} // namespace opennow
