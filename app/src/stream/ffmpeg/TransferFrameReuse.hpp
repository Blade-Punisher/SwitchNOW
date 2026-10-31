#pragma once
#include <array>
extern "C" {
#include <libavutil/frame.h>
}
namespace opennow::video {
// Keep exclusive software pixel storage, but discard every previous property.
// No new reference may escape until the transfer has completely overwritten it.
inline bool RetainTransferStorage(AVFrame* frame, int format, int width, int height,
                                  bool same_allocation_layout) {
    if (!frame || !same_allocation_layout || format != AV_PIX_FMT_NV12 ||
        frame->format != format || frame->width != width || frame->height != height ||
        width <= 0 || height <= 0 || !frame->buf[0] || !frame->data[0] || !frame->data[1] ||
        frame->extended_data != frame->data || frame->nb_extended_buf || frame->hw_frames_ctx ||
        frame->linesize[0] < width || frame->linesize[1] < width ||
        av_frame_is_writable(frame) <= 0) return false;
    std::array<AVBufferRef*, AV_NUM_DATA_POINTERS> buffers {};
    std::array<uint8_t*, AV_NUM_DATA_POINTERS> data {};
    std::array<int, AV_NUM_DATA_POINTERS> strides {};
    for (unsigned i=0; i<buffers.size(); ++i) {
        buffers[i]=frame->buf[i]; data[i]=frame->data[i]; strides[i]=frame->linesize[i];
        frame->buf[i]=nullptr; // move ownership out before unref resets metadata
    }
    av_frame_unref(frame);
    frame->format=format; frame->width=width; frame->height=height;
    for (unsigned i=0; i<buffers.size(); ++i) {
        frame->buf[i]=buffers[i]; frame->data[i]=data[i]; frame->linesize[i]=strides[i];
    }
    frame->extended_data=frame->data;
    return true;
}
}
