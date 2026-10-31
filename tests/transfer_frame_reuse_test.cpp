#include "stream/ffmpeg/TransferFrameReuse.hpp"
#include <cassert>
#include <cstring>
struct FakeBuffer { int refs=1; };
static int releases=0;
extern "C" int av_frame_is_writable(AVFrame* f) {
    return f->buf[0] && reinterpret_cast<FakeBuffer*>(f->buf[0]->buffer)->refs==1;
}
extern "C" void av_frame_unref(AVFrame* f) {
    for (auto* b:f->buf) if(b) { --reinterpret_cast<FakeBuffer*>(b->buffer)->refs; ++releases; }
    *f=AVFrame{}; f->format=AV_PIX_FMT_NONE; f->pts=AV_NOPTS_VALUE; f->extended_data=f->data;
}
int main() {
    FakeBuffer owner; unsigned char bytes[96]; std::memset(bytes,0xa5,sizeof(bytes));
    AVBufferRef ref {}; ref.buffer=reinterpret_cast<AVBuffer*>(&owner); ref.data=bytes;ref.size=sizeof(bytes);
    AVFrame f {}; f.format=AV_PIX_FMT_NV12; f.width=f.height=8;
    f.buf[0]=&ref; f.data[0]=bytes;f.data[1]=bytes+64;f.linesize[0]=f.linesize[1]=8;
    f.extended_data=f.data;f.pts=987;f.flags=123;
    using opennow::video::RetainTransferStorage;
    assert(!RetainTransferStorage(&f,AV_PIX_FMT_NV12,8,8,false) && f.pts==987);
    owner.refs=2;assert(!RetainTransferStorage(&f,AV_PIX_FMT_NV12,8,8,true)); owner.refs=1;
    assert(!RetainTransferStorage(&f,AV_PIX_FMT_NV12,16,8,true));
    assert(!RetainTransferStorage(&f,AV_PIX_FMT_YUV420P,8,8,true));
    f.linesize[1]=4;assert(!RetainTransferStorage(&f,AV_PIX_FMT_NV12,8,8,true));f.linesize[1]=8;
    assert(RetainTransferStorage(&f,AV_PIX_FMT_NV12,8,8,true));
    assert(owner.refs==1 && releases==0 && f.buf[0]==&ref && f.data[0]==bytes && f.data[1]==bytes+64);
    assert(f.pts==AV_NOPTS_VALUE && f.flags==0 && f.extended_data==f.data);
    for(auto b:bytes) assert(b==0xa5); // recycled storage itself is never copied/changed
    bytes[0]=0x11;assert(RetainTransferStorage(&f,AV_PIX_FMT_NV12,8,8,true) && bytes[0]==0x11);
    f.hw_frames_ctx=&ref;assert(!RetainTransferStorage(&f,AV_PIX_FMT_NV12,8,8,true));f.hw_frames_ctx=nullptr;
    av_frame_unref(&f);assert(owner.refs==0 && releases==1);
}
