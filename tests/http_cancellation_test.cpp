#include "http_cancellation.hpp"
#include <cassert>
#include <thread>

int main() {
    using opennow::HttpCancellationScope;
    assert(!HttpCancellationScope::Callback());
    bool cancelled = false;
    {
        HttpCancellationScope outer([&] { return cancelled; });
        assert(!HttpCancellationScope::Callback()());
        std::thread other([] { assert(!HttpCancellationScope::Callback()); });
        other.join(); // recovery cannot cancel another connection's requests
        {
            HttpCancellationScope inner([] { return false; });
            cancelled = true;
            assert(HttpCancellationScope::Callback()());
        }
        assert(HttpCancellationScope::Callback()());
    }
    assert(!HttpCancellationScope::Callback());
    try {
        HttpCancellationScope scope([] { return true; });
        throw 1;
    } catch (...) {}
    assert(!HttpCancellationScope::Callback()); // cleanup on refresh failure
}
