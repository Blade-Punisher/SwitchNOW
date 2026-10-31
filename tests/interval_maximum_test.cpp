#include "interval_maximum.hpp"
#include <cassert>
#include <thread>
#include <algorithm>
int main() {
    opennow::performance::IntervalMaximum value;
    value.Observe(100); value.Observe(50);
    assert(value.Take() == 100 && value.Take() == 0);
    std::thread producer([&] { for (unsigned i=1;i<=100000;++i) value.Observe(i); });
    std::uint64_t largest = 0;
    for (unsigned i=0;i<1000;++i) largest = std::max(largest, value.Take());
    producer.join(); largest = std::max(largest, value.Take());
    assert(largest == 100000 && value.Take() == 0);
}
