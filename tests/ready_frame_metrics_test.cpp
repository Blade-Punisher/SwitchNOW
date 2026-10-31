#include "stream/ReadyFrameMetrics.hpp"
#include <cassert>
int main() {
    opennow::video::ReadyFrameMetrics m;
    m.Observe(8000,1);m.Observe(8001,2);m.Observe(16000,2);m.Observe(25001,3);m.Observe(100001,9);
    assert(m.age_buckets[0]==1 && m.age_buckets[1]==2 && m.age_buckets[3]==1 && m.age_buckets[6]==1);
    assert(m.depth_buckets[1]==1 && m.depth_buckets[2]==2 && m.depth_buckets[3]==2);
}
