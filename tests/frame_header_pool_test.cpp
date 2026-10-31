#include "stream/FrameHeaderPool.hpp"
#include <cassert>
#include <memory>
#include <deque>
struct Frame { std::shared_ptr<int> pixels; int property=0; };
struct Ops {
    inline static int live=0; inline static bool fail=false;
    static Frame* Allocate() { ++live; return new Frame; }
    static int Reference(Frame* out,const Frame* source) { if (fail) return -1; *out=*source; return 0; }
    static void Unref(Frame* f) { f->pixels.reset(); f->property=0; }
    static void Free(Frame** f) { delete *f; *f=nullptr; --live; }
};
int main() {
    Frame source {std::make_shared<int>(123),456};
    using Pool=opennow::video::FrameHeaderPool<Frame,Ops,8>;
    {
        Pool pool; std::deque<Frame*> held;
        for (int i=0;i<10000;++i) {
            auto* frame=pool.Reference(&source); assert(frame && frame->pixels==source.pixels && frame->property==456);
            held.push_back(frame);
            if (held.size()>4) { auto* done=held.front(); held.pop_front(); pool.Recycle(done); assert(!done); }
        }
        const auto stats=pool.GetStats();
        assert(stats.allocated==5 && stats.reused==9995);
        for (auto* f:held) pool.Recycle(f);
        held.clear();
        assert(source.pixels.use_count()==1); // cached headers hold no pixel references
        Ops::fail=true; assert(!pool.Reference(&source)); Ops::fail=false;
        assert(!pool.Reference(nullptr));
        pool.Clear(); assert(Ops::live==0);
        // Exceed cache capacity with simultaneously held objects, then reclaim.
        for(int i=0;i<12;++i) held.push_back(pool.Reference(&source));
        for(auto* f:held) pool.Recycle(f);
        assert(pool.GetStats().cached==8 && Ops::live==8 && source.pixels.use_count()==1);
    }
    assert(Ops::live==0);
}
