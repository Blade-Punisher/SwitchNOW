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

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_nvtegra.h"
#include "libavutil/nvtegra_host1x.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "codec_desc.h"
#include "internal.h"
#include "decode.h"
#include "nvtegra_decode.h"

static void nvtegra_input_map_free(void *opaque, uint8_t *data) {
    AVNVTegraMap *map = (AVNVTegraMap *)data;

    if (!data)
        return;

    av_nvtegra_map_destroy(map);

    av_freep(&map);
}

static AVBufferRef *nvtegra_input_map_alloc(void *opaque, size_t size) {
    FFNVTegraDecodeContext *ctx = opaque;

    AVBufferRef  *buffer;
    AVNVTegraMap *map;
    int err;

    map = av_mallocz(sizeof(*map));
    if (!map)
        return NULL;

    err = av_nvtegra_map_create(map, ctx->channel, ctx->input_map_size, 0x100,
                                NVMAP_HEAP_IOVMM, NVMAP_HANDLE_WRITE_COMBINE);
    if (err < 0)
        return NULL;

    buffer = av_buffer_create((uint8_t *)map, sizeof(*map), nvtegra_input_map_free, ctx, 0);
    if (!buffer)
        goto fail;

    ctx->new_input_buffer = true;

    return buffer;

fail:
    av_log(ctx, AV_LOG_ERROR, "Failed to create buffer\n");
    av_nvtegra_map_destroy(map);
    av_freep(map);
    return NULL;
}

int ff_nvtegra_decode_init(AVCodecContext *avctx, FFNVTegraDecodeContext *ctx) {
    AVHWFramesContext      *frames_ctx;
    AVHWDeviceContext      *hw_device_ctx;
    AVNVTegraDeviceContext *device_hwctx;

    int err;

    err = ff_decode_get_hw_frames_ctx(avctx, AV_HWDEVICE_TYPE_NVTEGRA);
    if (err < 0)
        goto fail;

    frames_ctx    = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    hw_device_ctx = (AVHWDeviceContext *)frames_ctx->device_ref->data;
    device_hwctx  = hw_device_ctx->hwctx;

    if ((!ctx->is_nvjpg && !device_hwctx->nvdec_version) || (ctx->is_nvjpg && !device_hwctx->nvjpg_version))
        return AVERROR(EACCES);

    ctx->hw_device_ref = av_buffer_ref(frames_ctx->device_ref);
    if (!ctx->hw_device_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->decoder_pool = av_buffer_pool_init2(sizeof(AVNVTegraMap), ctx,
                                             nvtegra_input_map_alloc, NULL);
    if (!ctx->decoder_pool) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->channel = !ctx->is_nvjpg ? &device_hwctx->nvdec_channel : &device_hwctx->nvjpg_channel;

    err = av_nvtegra_cmdbuf_init(&ctx->cmdbuf);
    if (err < 0)
        goto fail;

    err = av_nvtegra_dfs_init(hw_device_ctx, ctx->channel, avctx->coded_width, avctx->coded_height,
                              av_q2d(avctx->framerate));
    if (err < 0)
        goto fail;

    return 0;

fail:
    ff_nvtegra_decode_uninit(avctx, ctx);
    return err;
}

int ff_nvtegra_decode_uninit(AVCodecContext *avctx, FFNVTegraDecodeContext *ctx) {
    AVHWFramesContext *frames_ctx;
    AVHWDeviceContext *hw_device_ctx;

    av_buffer_pool_uninit(&ctx->decoder_pool);

    av_buffer_unref(&ctx->hw_device_ref);

    av_nvtegra_cmdbuf_deinit(&ctx->cmdbuf);

    if (avctx->hw_frames_ctx) {
        frames_ctx    = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
        hw_device_ctx = (AVHWDeviceContext *)frames_ctx->device_ref->data;

        av_nvtegra_dfs_uninit(hw_device_ctx, ctx->channel);
    }


    return 0;
}

static void nvtegra_fdd_priv_free(void *priv) {
    FFNVTegraDecodeFrame    *tf = priv;
    FFNVTegraDecodeContext *ctx = tf->ctx;

    if (!tf)
        return;

    if (tf->in_flight)
        av_nvtegra_syncpt_wait(ctx->channel, tf->fence, -1);

    av_buffer_unref(&tf->input_map_ref);
    av_freep(&tf);
}

int ff_nvtegra_wait_decode(void *logctx, AVFrame *frame) {
    FrameDecodeData             *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame         *tf = fdd->hwaccel_priv;
    FFNVTegraDecodeContext      *ctx = tf->ctx;
    AVNVTegraMap          *input_map = (AVNVTegraMap *)tf->input_map_ref->data;
    AVHWDeviceContext *hw_device_ctx = (AVHWDeviceContext *)ctx->hw_device_ref->data;

    nvdec_status_s *nvdec_status;
    nvjpg_dec_status *nvjpg_status;
    uint32_t decode_cycles;
    uint8_t *mem;
    int err;

    if (!tf->in_flight)
        return 0;

    mem = av_nvtegra_map_get_addr(input_map);

    err = av_nvtegra_syncpt_wait(ctx->channel, tf->fence, -1);
    if (err < 0)
        return err;

    tf->in_flight = false;

    if (!ctx->is_nvjpg) {
        nvdec_status = (nvdec_status_s *)(mem + ctx->status_off);
        if (nvdec_status->error_status != 0 || nvdec_status->mbs_in_error != 0)
            return AVERROR_UNKNOWN;

        decode_cycles = nvdec_status->cycle_count * 16;
    } else {
        nvjpg_status = (nvjpg_dec_status *)(mem + ctx->status_off);
        if (nvjpg_status->error_status != 0 || nvjpg_status->bytes_offset == 0)
            return AVERROR_UNKNOWN;

        decode_cycles = nvjpg_status->cycle_count;
    }

    /* Decode time in µs: decode_cycles * 1000000 / ctx->channel->clock */
    err = av_nvtegra_dfs_update(hw_device_ctx, ctx->channel, tf->bitstream_len, decode_cycles);
    if (err < 0)
        return err;

    return 0;
}

int ff_nvtegra_start_frame(AVCodecContext *avctx, AVFrame *frame, FFNVTegraDecodeContext *ctx) {
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    FrameDecodeData          *fdd = (FrameDecodeData *)frame->private_ref->data;

    FFNVTegraDecodeFrame *tf = NULL;
    int err;

    /* Abort on resolution changes that wouldn't fit into the frame */
    if ((frame->width > frames_ctx->width) || (frame->height > frames_ctx->height))
        return AVERROR(EINVAL);

    ctx->bitstream_len = ctx->num_slices = 0;

    if (fdd->hwaccel_priv) {
        /*
        * For interlaced video, both fields use the same fdd,
        * however by proceeding we might overwrite the input buffer
        * during the decoding, so wait for the previous operation to complete.
        */
       err = ff_nvtegra_wait_decode(avctx, frame);
        if (err < 0)
            return err;
    } else {
        tf = av_mallocz(sizeof(*tf));
        if (!tf)
            return AVERROR(ENOMEM);

        fdd->hwaccel_priv      = tf;
        fdd->hwaccel_priv_free = nvtegra_fdd_priv_free;
        fdd->post_process      = ff_nvtegra_wait_decode;

        tf->ctx = ctx;

        tf->input_map_ref = av_buffer_pool_get(ctx->decoder_pool);
        if (!tf->input_map_ref) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }

    tf = fdd->hwaccel_priv;
    tf->in_flight = false;

    err = av_nvtegra_cmdbuf_add_memory(&ctx->cmdbuf, (AVNVTegraMap *)tf->input_map_ref->data,
                                       ctx->cmdbuf_off, ctx->max_cmdbuf_size);
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_clear(&ctx->cmdbuf);
    if (err < 0)
        return err;

    return 0;

fail:
    nvtegra_fdd_priv_free(tf);
    return err;
}

int ff_nvtegra_decode_slice(AVCodecContext *avctx, AVFrame *frame,
                            const uint8_t *buf, uint32_t buf_size, bool add_startcode)
{
    FFNVTegraDecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    FrameDecodeData        *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame    *tf = fdd->hwaccel_priv;
    AVNVTegraMap     *input_map = (AVNVTegraMap *)tf->input_map_ref->data;

    bool need_bitstream_move = false;
    uint32_t old_bitstream_off, startcode_size;
    uint8_t *mem;
    int err;

    mem = av_nvtegra_map_get_addr(input_map);

    startcode_size = add_startcode ? 3 : 0;

    /* Reserve 16 bytes for the termination sequence */
    if (ctx->bitstream_len + buf_size + startcode_size >= ctx->max_bitstream_size - 16) {
        ctx->input_map_size += ctx->max_bitstream_size + buf_size;
        ctx->input_map_size  = FFALIGN(ctx->input_map_size, 0x1000);

        ctx->max_bitstream_size = ctx->input_map_size - ctx->bitstream_off;

        need_bitstream_move = false;
    }

    /* Reserve 4 bytes for the bitstream size */
    if (ctx->max_num_slices &&  ctx->num_slices >= ctx->max_num_slices - 1) {
        ctx->input_map_size += ctx->max_num_slices * sizeof(uint32_t);
        ctx->input_map_size  = FFALIGN(ctx->input_map_size, 0x1000);

        ctx->max_num_slices *= 2;

        old_bitstream_off = ctx->bitstream_off;
        ctx->bitstream_off = ctx->slice_offsets_off + ctx->max_num_slices * sizeof(uint32_t);

        need_bitstream_move = true;
    }

    if (ctx->input_map_size != av_nvtegra_map_get_size(input_map)) {
        err = av_nvtegra_map_realloc(input_map, ctx->input_map_size, 0x100,
                                     NVMAP_HEAP_IOVMM, NVMAP_HANDLE_WRITE_COMBINE);
        if (err < 0)
            return err;

        mem = av_nvtegra_map_get_addr(input_map);

        err = av_nvtegra_cmdbuf_add_memory(&ctx->cmdbuf, input_map,
                                           ctx->cmdbuf_off, ctx->max_cmdbuf_size);
        if (err < 0)
            return err;

        /* Running out of slice offsets mem shouldn't happen so the extra memmove is fine */
        if (need_bitstream_move)
            memmove(mem + ctx->bitstream_off, mem + old_bitstream_off, ctx->bitstream_len);
    }

    if (ctx->max_num_slices)
        ((uint32_t *)(mem + ctx->slice_offsets_off))[ctx->num_slices] = ctx->bitstream_len;

    /* NAL startcode 000001 */
    if (add_startcode) {
        AV_WB24(mem + ctx->bitstream_off + ctx->bitstream_len, 1);
        ctx->bitstream_len += 3;
    }

    memcpy(mem + ctx->bitstream_off + ctx->bitstream_len, buf, buf_size);
    ctx->bitstream_len += buf_size;

    ctx->num_slices++;

    return 0;
}

int ff_nvtegra_end_frame(AVCodecContext *avctx, AVFrame *frame, FFNVTegraDecodeContext *ctx,
                         const uint8_t *end_sequence, int end_sequence_size)
{
    FrameDecodeData     *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame *tf = fdd->hwaccel_priv;
    AVNVTegraMap  *input_map = (AVNVTegraMap *)tf->input_map_ref->data;

    uint8_t *mem;
    int err;

    mem = av_nvtegra_map_get_addr(input_map);

    /* Last slice data range */
    if (ctx->max_num_slices)
        ((uint32_t *)(mem + ctx->slice_offsets_off))[ctx->num_slices] = ctx->bitstream_len;

    /* Termination sequence for the bitstream data */
    if (end_sequence_size)
        memcpy(mem + ctx->bitstream_off + ctx->bitstream_len, end_sequence, end_sequence_size);

    err = av_nvtegra_cmdbuf_begin(&ctx->cmdbuf, !ctx->is_nvjpg ? HOST1X_CLASS_NVDEC : HOST1X_CLASS_NVJPG);
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_add_syncpt_incr(&ctx->cmdbuf, ctx->channel->syncpt, 0);
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_end(&ctx->cmdbuf);
    if (err < 0)
        return err;

    err = av_nvtegra_channel_submit(ctx->channel, &ctx->cmdbuf, &tf->fence);
    if (err < 0)
        return err;

    tf->bitstream_len = ctx->bitstream_len;
    tf->in_flight     = true;

    ctx->frame_idx++;

    ctx->new_input_buffer = false;

    return 0;
}

static int nvtegra_get_size_constraints(enum AVCodecID codec,
                                        int *min_width, int *min_height,
                                        int *max_width, int *max_height,
                                        int *align, int *max_mbs)
{
    switch (codec) {
        case AV_CODEC_ID_MPEG1VIDEO:
        case AV_CODEC_ID_MPEG2VIDEO:
            *min_width = 48,    *min_height = 1;
            *max_width = 4096,  *max_height = 4096;
            *align     = 16,    *max_mbs    = 0x20000;
            break;

        case AV_CODEC_ID_MPEG4:
            *min_width = 48,    *min_height = 1;
            *max_width = 2048,  *max_height = 2048;
            *align     = 16,    *max_mbs    = 0x2000;
            break;

        case AV_CODEC_ID_VC1:
        case AV_CODEC_ID_WMV3:
            *min_width = 48,    *min_height = 1;
            *max_width = 2048,  *max_height = 2048;
            *align     = 1,     *max_mbs    = -1;
            break;

        case AV_CODEC_ID_H264:
            *min_width = 48,    *min_height = 1;
            *max_width = 4096,  *max_height = 4096;
            *align     = 16,    *max_mbs    = 0x20000;
            break;

        case AV_CODEC_ID_HEVC:
            /* Note: on nvdec 4.0+ (tegra 194) max dimensions are 8192, and max mbs 0x80000 */
            *min_width = 144,   *min_height = 144;
            *max_width = 4096,  *max_height = 4096;
            *align     = 64,    *max_mbs    = 0x20000;
            break;

        case AV_CODEC_ID_VP8:
            *min_width = 48,    *min_height = 1;
            *max_width = 4096,  *max_height = 4096;
            *align     = 16,    *max_mbs    = 0x20000;
            break;

        case AV_CODEC_ID_VP9:
            /* Note: on nvdec 4.0+ (tegra 194) max dimensions are 8192, and max mbs 0x40000 */
            *min_width = 144,   *min_height = 144;
            *max_width = 4096,  *max_height = 4096;
            *align     = 16,    *max_mbs    = 0x10000;
            break;

        case AV_CODEC_ID_MJPEG:
            *min_width = 1,     *min_height = 1;
            *max_width = 16384, *max_height = 16384;
            *align     = 1,     *max_mbs    = -1;
            break;

        #if 0
        case AV_CODEC_ID_AV1:
            /* Note: on nvdec 4.0+ (tegra 194) max dimensions are 8192, and max mbs 0x80000 */
            *min_width = 128,   *min_height = 128;
            *max_width = 4096,  *max_height = 4096;
            *align     = 64,    *max_mbs    = 0x20000;
            break;
        #endif

        default:
            return AVERROR(EINVAL);
    }

    return 0;
}

int ff_nvtegra_frame_params(AVCodecContext *avctx, AVBufferRef *hw_frames_ctx) {
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)hw_frames_ctx->data;
    const AVPixFmtDescriptor *sw_desc;

    int min_width, min_height, max_width, max_height, align, max_mbs,
        aligned_width, aligned_height, num_mbs;
    int err;

    err = nvtegra_get_size_constraints(avctx->codec_id, &min_width, &min_height,
                                       &max_width, &max_height, &align, &max_mbs);
    if (err < 0)
        return err;

    aligned_width  = FFALIGN(avctx->coded_width,  align);
    aligned_height = FFALIGN(avctx->coded_height, align);
    num_mbs = (aligned_width / 16) * (aligned_height / 16);

    if ((aligned_width  < min_width)  || (aligned_width  > max_width) ||
        (aligned_height < min_height) || (aligned_height > max_height))
    {
        av_log(avctx, AV_LOG_ERROR, "Dimensions %dx%d (min. %dx%d, max. %dx%d) "
                                    "are not supported by the hardware for codec %s\n",
               avctx->coded_width, avctx->coded_height,
               min_width, min_height, max_width, max_height,
               avctx->codec_descriptor->name);
        return AVERROR(EINVAL);
    }

    if ((max_mbs > 0) && (num_mbs > max_mbs)) {
        av_log(avctx, AV_LOG_ERROR, "Number of macroblocks %d exceeds maximum %d "
                                    "for codec %s\n",
               num_mbs, max_mbs, avctx->codec_descriptor->name);
        return AVERROR(EINVAL);
    }

    frames_ctx->format = AV_PIX_FMT_NVTEGRA;
    frames_ctx->width  = FFALIGN(avctx->coded_width,  2); /* NVDEC only supports even sizes */
    frames_ctx->height = FFALIGN(avctx->coded_height, 2);

    sw_desc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!sw_desc)
        return AVERROR_BUG;

    switch (sw_desc->comp[0].depth) {
        case 8:
            frames_ctx->sw_format = (sw_desc->nb_components > 1) ?
                                    AV_PIX_FMT_NV12 : AV_PIX_FMT_GRAY8;
            break;
        case 10:
            frames_ctx->sw_format = (sw_desc->nb_components > 1) ?
                                    AV_PIX_FMT_P010 : AV_PIX_FMT_GRAY10;
            break;
        default:
            return AVERROR(EINVAL);
    }

    return 0;
}
