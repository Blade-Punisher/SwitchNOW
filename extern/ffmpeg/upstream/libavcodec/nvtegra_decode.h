/*
 * Copyright (c) 2024 averne <averne381@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef AVCODEC_NVTEGRA_DECODE_H
#define AVCODEC_NVTEGRA_DECODE_H

#include <stdbool.h>

#include "avcodec.h"
#include "libavutil/mem.h"
#include "libavutil/hwcontext_nvtegra.h"

#include "libavutil/nvdec_drv.h"
#include "libavutil/nvjpg_drv.h"
#include "libavutil/clc5b0.h"
#include "libavutil/cle7d0.h"

typedef struct FFNVTegraDecodeContext {
    uint64_t frame_idx;

    AVBufferRef *hw_device_ref;
    AVBufferPool *decoder_pool;

    bool is_nvjpg;
    AVNVTegraChannel *channel;

    AVNVTegraCmdbuf cmdbuf;

    uint32_t pic_setup_off, status_off, cmdbuf_off,
             bitstream_off, slice_offsets_off;
    uint32_t input_map_size;
    uint32_t max_cmdbuf_size, max_bitstream_size, max_num_slices;

    uint32_t num_slices;
    uint32_t bitstream_len;

    bool new_input_buffer;
} FFNVTegraDecodeContext;

typedef struct FFNVTegraDecodeFrame {
    FFNVTegraDecodeContext *ctx;
    AVBufferRef *input_map_ref;
    uint32_t fence;
    uint32_t bitstream_len;
    bool in_flight;
} FFNVTegraDecodeFrame;

static inline size_t ff_nvtegra_decode_pick_bitstream_buffer_size(AVCodecContext *avctx) {
    /*
     * Official software uses a static map of a predetermined size, usually around 0x600000 (6MiB).
     * Our implementation supports dynamically resizing the input map, so be less conservative.
     */
    if ((avctx->coded_width >= 3840) || (avctx->coded_height >= 2160))  /* 4k */
        return 0x100000;                                                /* 1MiB */
    if ((avctx->coded_width >= 1920) || (avctx->coded_height >= 1080))  /* 1080p */
        return 0x40000;                                                 /* 256KiB */
    else
        return 0x10000;                                                 /* 64KiB */
}

static inline AVFrame *ff_nvtegra_safe_get_ref(AVFrame *ref, AVFrame *fallback) {
    return (ref && ref->private_ref) ? ref : fallback;
}

int ff_nvtegra_decode_init(AVCodecContext *avctx, FFNVTegraDecodeContext *ctx);
int ff_nvtegra_decode_uninit(AVCodecContext *avctx, FFNVTegraDecodeContext *ctx);
int ff_nvtegra_start_frame(AVCodecContext *avctx, AVFrame *frame, FFNVTegraDecodeContext *ctx);
int ff_nvtegra_decode_slice(AVCodecContext *avctx, AVFrame *frame,
                            const uint8_t *buf, uint32_t buf_size, bool add_startcode);
int ff_nvtegra_end_frame(AVCodecContext *avctx, AVFrame *frame, FFNVTegraDecodeContext *ctx,
                         const uint8_t *end_sequence, int end_sequence_size);

int ff_nvtegra_wait_decode(void *logctx, AVFrame *frame);

int ff_nvtegra_frame_params(AVCodecContext *avctx, AVBufferRef *hw_frames_ctx);

#endif /* AVCODEC_NVTEGRA_DECODE_H */
