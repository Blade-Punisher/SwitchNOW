// Real queue implementation with a reference-counting FFmpeg test double.
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "stream/ffmpeg/DecodedFrameGate.hpp"
#include <condition_variable>
#include <thread>
#include <mutex>
#include <cassert>
#include <cstring>
static int headers_live=0, refs_live=0;
static unsigned char pixels[96];
extern "C" AVFrame* av_frame_alloc(void) {
    ++headers_live; auto* f=new AVFrame{};f->format=AV_PIX_FMT_NONE;f->extended_data=f->data;return f;
}
extern "C" void av_frame_unref(AVFrame* f) {
    for(auto* ref:f->buf) if(ref) { delete ref;--refs_live; }
    *f=AVFrame{};f->format=AV_PIX_FMT_NONE;f->extended_data=f->data;
}
extern "C" void av_frame_free(AVFrame** f) {
    if(!*f) return;
    av_frame_unref(*f);delete *f;*f=nullptr;--headers_live;
}
extern "C" int av_frame_ref(AVFrame* out,const AVFrame* source) {
    *out=*source;out->extended_data=out->data;
    for(unsigned i=0;i<AV_NUM_DATA_POINTERS;++i) if(source->buf[i]) {
        out->buf[i]=new AVBufferRef(*source->buf[i]);++refs_live;
    }
    return 0;
}
int main() {
    auto* source=av_frame_alloc();source->format=AV_PIX_FMT_NV12;source->width=source->height=8;
    source->data[0]=pixels;source->data[1]=pixels+64;
    source->linesize[0]=source->linesize[1]=8;
    source->buf[0]=new AVBufferRef{};source->buf[0]->data=pixels;source->buf[0]->size=sizeof(pixels);++refs_live;
    std::memset(pixels,0xa5,sizeof(pixels));
    AVFrameQueue queue;
    source->pts=1;queue.push(source);source->pts=2;queue.push(source);
    bool reused=false;uint64_t generation=0;
    for(int i=3;i<1003;++i) {
        source->pts=i;queue.push(source);
        auto* frame=queue.pop(reused,generation);
        assert(frame && !reused && frame->pts==i-2 && generation==static_cast<uint64_t>(i-2));
        assert(frame->data[0]==pixels && frame->data[1]==pixels+64);
    }
    assert(queue.headerStats().allocated<=5 && queue.headerStats().reused>990);
    assert(queue.readyMetrics().depth_buckets[3]==1000);
    queue.cleanup();assert(headers_live==1 && refs_live==1);
    // Overflow preserves the latest capacity without leaking any references.
    for(int i=1;i<=12;++i) {source->pts=i;queue.push(source);}
    assert(queue.getOverflowDropStat()==9);
    for(int i=10;i<=12;++i) assert(queue.pop(reused,generation)->pts==i && !reused);
    assert(queue.pop(reused,generation) && reused);
    queue.cleanup();assert(headers_live==1 && refs_live==1);
    for(auto value:pixels)assert(value==0xa5);
    // Exercise the production publication gate and actual ready queue with
    // reference-counted FFmpeg frames. No GPU or pixel copying is involved.
    auto& holder=AVFrameHolder::instance();
    holder.prepare(3,1,std::chrono::microseconds(0));
    opennow::video::DecodedFrameGate gate;
    unsigned faults=0,verified=0;
    gate.SetObserver([&](const auto& event) { if(event.fault) ++faults; else ++verified; });
    source->pts=1000;
    assert(gate.Queue(1000,true) && gate.Begin(1000,true));
    assert(gate.Required());
    assert(gate.Publish(source) && !gate.Required());
    source->pts=2500;assert(gate.Publish(source));
    holder.get([&](AVFrame* f,uint64_t,bool reused_frame) { assert(f->pts==1000 && !reused_frame); });
    source->pts=5500;assert(gate.Publish(source));
    // A lost picture invalidates only pending frames at/after its timestamp.
    assert(gate.Fault(true,4000));
    assert(gate.PrunedReady()==1);
    holder.get([&](AVFrame* f,uint64_t,bool reused_frame) { assert(f->pts==2500 && !reused_frame); });
    holder.get([&](AVFrame* f,uint64_t,bool reused_frame) { assert(f->pts==2500 && reused_frame); });
    assert(gate.Queue(6000,true) && gate.Begin(6000,true));
    source->pts=6500;assert(!gate.Publish(source)); // zero-output IDR has not confirmed
    assert(gate.Fault(true,7000));
    source->pts=6000;assert(!gate.Publish(source)); // late old recovery completion
    assert(gate.Required());
    assert(gate.Queue(8000,true) && gate.Begin(8000,true));
    source->pts=8000;assert(gate.Publish(source));
    source->pts=9500;assert(gate.Publish(source));
    assert(!gate.Required() && verified==2);
    // A good + corrupt + good drain must signal damage even when the decoder
    // returns a positive produced count for the good picture.
    source->pts=11000;source->decode_error_flags=1;
    assert(!gate.Publish(source));
    source->decode_error_flags=0;source->pts=12500;
    assert(!gate.Publish(source) && gate.Required());
    assert(gate.Corrupt()==1 && faults==3);
    assert(gate.Queue(14000,true) && gate.Begin(14000,true));
    source->pts=AV_NOPTS_VALUE;assert(!gate.Publish(source));
    assert(gate.UnknownPts()==1 && gate.Required());
    source->pts=14000;assert(gate.Publish(source));
    assert(!gate.Required());

    // Serialize a newer network fault against an IDR observer that has just
    // confirmed output. Its completion must never clear the newer fault.
    std::mutex barrier_mutex;std::condition_variable barrier_cv;
    bool observer_entered=false,release_observer=false;
    gate.SetObserver([&](const auto& event) {
        if(event.fault) return;
        std::unique_lock<std::mutex> lock(barrier_mutex);
        observer_entered=true;barrier_cv.notify_all();
        barrier_cv.wait(lock,[&] { return release_observer; });
    });
    assert(gate.Queue(15500,true) && gate.Begin(15500,true));
    source->pts=15500;
    std::thread output([&] { assert(gate.Publish(source)); });
    {
        std::unique_lock<std::mutex> lock(barrier_mutex);
        barrier_cv.wait(lock,[&] { return observer_entered; });
    }
    std::thread newer_loss([&] { assert(gate.Fault(true,17000)); });
    {
        std::lock_guard<std::mutex> lock(barrier_mutex);release_observer=true;
    }
    barrier_cv.notify_all();output.join();newer_loss.join();
    assert(gate.Required());
    gate.SetObserver({});
    // A corrupt older output drained before the already-submitted newer IDR
    // must not force another request when that IDR covers the known damage.
    assert(gate.Queue(20000,true) && gate.Begin(20000,true));
    source->pts=18500;source->flags=AV_FRAME_FLAG_CORRUPT;
    assert(!gate.Publish(source));source->flags=0;
    source->pts=20000;assert(gate.Publish(source) && !gate.Required());
    holder.cleanup();
    assert(headers_live==1 && refs_live==1);
    opennow::video::DecodedFrameGate steady;
    source->pts=1000;assert(steady.Queue(1000,true) && steady.Begin(1000,true));
    assert(steady.Publish(source));
    holder.get([](AVFrame*,uint64_t,bool) {});
    const auto allocations_before=holder.getHeaderPoolStats().allocated;
    for(int i=1;i<=10000;++i) {
        const auto timestamp=static_cast<uint32_t>(1000+i*1500);
        source->pts=timestamp;
        assert(steady.Queue(timestamp,false) && steady.Begin(timestamp,false));
        assert(steady.Publish(source));
        holder.get([&](AVFrame* f,uint64_t,bool repeat) { assert(!repeat && f->pts==timestamp); });
    }
    assert(steady.Quarantined()==0 && steady.Faults()==0 && steady.Verified()==1);
    assert(holder.getHeaderPoolStats().allocated<=allocations_before+1);
    holder.cleanup();
    assert(headers_live==1 && refs_live==1);
    for(auto value:pixels)assert(value==0xa5);
    av_frame_free(&source);assert(headers_live==0 && refs_live==0);
}
