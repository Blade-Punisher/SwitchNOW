#include "network_probe_budget.hpp"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
int main() {
    using namespace std::chrono;
    using namespace opennow::network;
    ProbeBudget budget(3);
    const auto deadline = steady_clock::now() + seconds(5);
    {
        ProbeLease a(budget,deadline), b(budget,deadline), c(budget,deadline);
        assert(a && b && c);
        ProbeLease fourth(budget,steady_clock::now() + milliseconds(10));
        assert(!fourth);
    }
    std::atomic<unsigned> active {0}, maximum {0};
    std::vector<std::thread> workers;
    for (unsigned i=0;i<12;++i) workers.emplace_back([&] {
        ProbeLease lease(budget,deadline); assert(lease);
        const auto count = active.fetch_add(1) + 1;
        auto previous = maximum.load();
        while (count > previous && !maximum.compare_exchange_weak(previous,count)) {}
        std::this_thread::sleep_for(milliseconds(2));
        --active;
    });
    for (auto& worker : workers) worker.join();
    assert(maximum > 0 && maximum <= 3 && active == 0);
    ProbeLease released(budget,steady_clock::now() + milliseconds(50)); assert(released);
}
