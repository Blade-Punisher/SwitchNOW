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

#include "config_components.h"

#include <stdbool.h>

#include "avcodec.h"
#include "hwaccel_internal.h"
#include "internal.h"
#include "hwconfig.h"
#include "mpeg4video.h"
#include "mpeg4videodec.h"
#include "mpeg4videodefs.h"
#include "decode.h"
#include "nvtegra_decode.h"

#include "libavutil/pixdesc.h"
#include "libavutil/nvtegra_host1x.h"

typedef struct NVTegraMPEG4DecodeContext {
    FFNVTegraDecodeContext core;

    AVNVTegraMap common_map;
    uint32_t coloc_off, history_off, scratch_off;
    uint32_t history_size, scratch_size;

    AVFrame *prev_frame, *next_frame;
} NVTegraMPEG4DecodeContext;

/* Size (width, height) of a macroblock */
#define MB_SIZE 16

static const uint8_t bitstream_end_sequence[16] = {
    0x00, 0x00, 0x01, 0xb1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xb1, 0x00, 0x00, 0x00, 0x00,
};

static int nvtegra_mpeg4_decode_uninit(AVCodecContext *avctx) {
    NVTegraMPEG4DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    int err;

    av_log(avctx, AV_LOG_DEBUG, "Deinitializing NVTEGRA MPEG4 decoder\n");

    err = av_nvtegra_map_destroy(&ctx->common_map);
    if (err < 0)
        return err;

    err = ff_nvtegra_decode_uninit(avctx, &ctx->core);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_mpeg4_decode_init(AVCodecContext *avctx) {
    NVTegraMPEG4DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    AVHWDeviceContext      *hw_device_ctx;
    AVNVTegraDeviceContext *device_hwctx;
    uint32_t width_in_mbs, height_in_mbs,
             coloc_size, history_size, scratch_size, common_map_size;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Initializing NVTEGRA MPEG4 decoder\n");

    /* Ignored: histogram map, size 0x400 */
    ctx->core.pic_setup_off  = 0;
    ctx->core.status_off     = FFALIGN(ctx->core.pic_setup_off + sizeof(nvdec_mpeg4_pic_s),
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.cmdbuf_off     = FFALIGN(ctx->core.status_off    + sizeof(nvdec_status_s),
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.bitstream_off  = FFALIGN(ctx->core.cmdbuf_off    + AV_NVTEGRA_MAP_ALIGN,
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.input_map_size = FFALIGN(ctx->core.bitstream_off + ff_nvtegra_decode_pick_bitstream_buffer_size(avctx),
                                       0x1000);

    ctx->core.max_cmdbuf_size    = ctx->core.bitstream_off  - ctx->core.cmdbuf_off;
    ctx->core.max_bitstream_size = ctx->core.input_map_size - ctx->core.bitstream_off;

    err = ff_nvtegra_decode_init(avctx, &ctx->core);
    if (err < 0)
        goto fail;

    hw_device_ctx = (AVHWDeviceContext *)ctx->core.hw_device_ref->data;
    device_hwctx  = hw_device_ctx->hwctx;

    width_in_mbs  = FFALIGN(avctx->coded_width,  MB_SIZE) / MB_SIZE;
    height_in_mbs = FFALIGN(avctx->coded_height, MB_SIZE) / MB_SIZE;
    coloc_size    = FFALIGN(FFALIGN(height_in_mbs, 2) * (width_in_mbs * 64) - 63, 0x100);
    history_size  = FFALIGN(width_in_mbs * 0x100 + 0x1100, 0x100);
    scratch_size  = 0x400;

    ctx->coloc_off   = 0;
    ctx->history_off = FFALIGN(ctx->coloc_off   + coloc_size,   AV_NVTEGRA_MAP_ALIGN);
    ctx->scratch_off = FFALIGN(ctx->history_off + history_size, AV_NVTEGRA_MAP_ALIGN);
    common_map_size  = FFALIGN(ctx->scratch_off + scratch_size, 0x1000);

    err = av_nvtegra_map_create(&ctx->common_map, &device_hwctx->nvdec_channel, common_map_size, 0x100,
                                NVMAP_HEAP_IOVMM, NVMAP_HANDLE_WRITE_COMBINE);
    if (err < 0)
        goto fail;

    ctx->history_size = history_size;
    ctx->scratch_size = scratch_size;

    return 0;

fail:
    nvtegra_mpeg4_decode_uninit(avctx);
    return err;
}

static void nvtegra_mpeg4_prepare_frame_setup(nvdec_mpeg4_pic_s *setup, AVCodecContext *avctx,
                                              NVTegraMPEG4DecodeContext *ctx)
{
    Mpeg4DecContext *m = avctx->priv_data;
    MpegEncContext  *s = &m->m;

    int i;

    *setup = (nvdec_mpeg4_pic_s){
        .scratch_pic_buffer_size      = ctx->scratch_size,

        .gptimer_timeout_value        = 0, /* Default value */

        .FrameWidth                   = FFALIGN(s->width,  MB_SIZE),
        .FrameHeight                  = FFALIGN(s->height, MB_SIZE),

        .vop_time_increment_bitcount  = m->time_increment_bits,
        .resync_marker_disable        = !m->resync_marker,

        .tileFormat                   = 0, /* TBL */
        .gob_height                   = 0, /* GOB_2 */

        .width                        = FFALIGN(s->width,  MB_SIZE),
        .height                       = FFALIGN(s->height, MB_SIZE),

        .FrameStride                  = {
            s->cur_pic.ptr->f->linesize[0],
            s->cur_pic.ptr->f->linesize[1],
        },

        .luma_top_offset              = 0,
        .luma_bot_offset              = 0,
        .luma_frame_offset            = 0,
        .chroma_top_offset            = 0,
        .chroma_bot_offset            = 0,
        .chroma_frame_offset          = 0,

        .HistBufferSize               = ctx->history_size / 256,

        .trd                          = { s->pp_time, s->pp_field_time >> 1 },
        .trb                          = { s->pb_time, s->pb_field_time >> 1 },

        .vop_fcode_forward            = s->f_code,
        .vop_fcode_backward           = s->b_code,

        .interlaced                   = s->interlaced_dct,
        .quant_type                   = s->mpeg_quant,
        .quarter_sample               = s->quarter_sample,
        .short_video_header           = avctx->codec->id == AV_CODEC_ID_H263,

        .curr_output_memory_layout    = 0, /* NV12 */

        .ptype                        = s->pict_type - AV_PICTURE_TYPE_I,
        .rnd                          = s->no_rounding,
        .alternate_vertical_scan_flag = s->alternate_scan,

        .ref_memory_layout            = { 0, 0 }, /* NV12 */
    };

    for (i = 0; i < 64; ++i) {
        setup->intra_quant_mat   [i] = s->intra_matrix[i];
        setup->nonintra_quant_mat[i] = s->inter_matrix[i];
    }
}

static int nvtegra_mpeg4_prepare_cmdbuf(AVNVTegraCmdbuf *cmdbuf, MpegEncContext *s, NVTegraMPEG4DecodeContext *ctx,
                                        AVFrame *cur_frame, AVFrame *prev_frame, AVFrame *next_frame)
{
    FrameDecodeData     *fdd = (FrameDecodeData *)cur_frame->private_ref->data;
    FFNVTegraDecodeFrame *tf = fdd->hwaccel_priv;
    AVNVTegraMap  *input_map = (AVNVTegraMap *)tf->input_map_ref->data;

    int err;

    err = av_nvtegra_cmdbuf_begin(cmdbuf, HOST1X_CLASS_NVDEC);
    if (err < 0)
        return err;

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_APPLICATION_ID,
                          AV_NVTEGRA_ENUM(NVC5B0_SET_APPLICATION_ID, ID, MPEG4));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_CONTROL_PARAMS,
                          AV_NVTEGRA_ENUM (NVC5B0_SET_CONTROL_PARAMS, CODEC_TYPE,     MPEG4) |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, ERR_CONCEAL_ON, 1)     |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, GPTIMER_ON,     1));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_PICTURE_INDEX,
                          AV_NVTEGRA_VALUE(NVC5B0_SET_PICTURE_INDEX, INDEX, ctx->core.frame_idx));

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_DRV_PIC_SETUP_OFFSET,
                          input_map,        ctx->core.pic_setup_off, NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_IN_BUF_BASE_OFFSET,
                          input_map,        ctx->core.bitstream_off, NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_NVDEC_STATUS_OFFSET,
                          input_map,        ctx->core.status_off,    NVHOST_RELOC_TYPE_DEFAULT);

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_COLOC_DATA_OFFSET,
                          &ctx->common_map, ctx->coloc_off,          NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_HISTORY_OFFSET,
                          &ctx->common_map, ctx->history_off,        NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PIC_SCRATCH_BUF_OFFSET,
                          &ctx->common_map, ctx->scratch_off,        NVHOST_RELOC_TYPE_DEFAULT);

#define PUSH_FRAME(fr, offset) ({                                                           \
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PICTURE_LUMA_OFFSET0   + offset * 4,           \
                          av_nvtegra_frame_get_fbuf_map(fr), 0, NVHOST_RELOC_TYPE_DEFAULT); \
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PICTURE_CHROMA_OFFSET0 + offset * 4,           \
                          av_nvtegra_frame_get_fbuf_map(fr), fr->data[1] - fr->data[0],     \
                          NVHOST_RELOC_TYPE_DEFAULT);                                       \
})

    PUSH_FRAME(cur_frame,  0);
    PUSH_FRAME(prev_frame, 1);
    PUSH_FRAME(next_frame, 2);

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_EXECUTE,
                          AV_NVTEGRA_ENUM(NVC5B0_EXECUTE, AWAKEN, ENABLE));

    err = av_nvtegra_cmdbuf_end(cmdbuf);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_mpeg4_start_frame(AVCodecContext *avctx, const uint8_t *buf, uint32_t buf_size) {
    Mpeg4DecContext             *m = avctx->priv_data;
    MpegEncContext              *s = &m->m;
    AVFrame                 *frame = s->cur_pic.ptr->f;
    FrameDecodeData           *fdd = (FrameDecodeData *)frame->private_ref->data;
    NVTegraMPEG4DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    FFNVTegraDecodeFrame *tf;
    AVNVTegraMap *input_map;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Starting MPEG4-NVTEGRA frame with pixel format %s\n",
           av_get_pix_fmt_name(avctx->sw_pix_fmt));

    err = ff_nvtegra_start_frame(avctx, frame, &ctx->core);
    if (err < 0)
        return err;

    tf = fdd->hwaccel_priv;
    input_map = (AVNVTegraMap *)tf->input_map_ref->data;
    mem = av_nvtegra_map_get_addr(input_map);

    nvtegra_mpeg4_prepare_frame_setup((nvdec_mpeg4_pic_s *)(mem + ctx->core.pic_setup_off), avctx, ctx);

    ctx->prev_frame = (s->pict_type != AV_PICTURE_TYPE_I && s->last_pic.ptr) ? s->last_pic.ptr->f : frame;
    ctx->next_frame = (s->pict_type == AV_PICTURE_TYPE_B && s->next_pic.ptr) ? s->next_pic.ptr->f : frame;

    return 0;
}

static int nvtegra_mpeg4_end_frame(AVCodecContext *avctx) {
    Mpeg4DecContext             *m = avctx->priv_data;
    MpegEncContext              *s = &m->m;
    NVTegraMPEG4DecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    AVFrame                 *frame = s->cur_pic.ptr->f;
    FrameDecodeData           *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame       *tf = fdd->hwaccel_priv;

    nvdec_mpeg4_pic_s *setup;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Ending MPEG4-NVTEGRA frame with %u slices -> %u bytes\n",
           ctx->core.num_slices, ctx->core.bitstream_len);

    if (!tf || !ctx->core.num_slices)
        return 0;

    mem = av_nvtegra_map_get_addr((AVNVTegraMap *)tf->input_map_ref->data);

    setup = (nvdec_mpeg4_pic_s *)(mem + ctx->core.pic_setup_off);
    setup->stream_len  = ctx->core.bitstream_len + sizeof(bitstream_end_sequence);
    setup->slice_count = ctx->core.num_slices;

    err = nvtegra_mpeg4_prepare_cmdbuf(&ctx->core.cmdbuf, s, ctx, frame,
                                       ctx->prev_frame, ctx->next_frame);
    if (err < 0)
        return err;

    return ff_nvtegra_end_frame(avctx, frame, &ctx->core, bitstream_end_sequence,
                                sizeof(bitstream_end_sequence));
}

static int nvtegra_mpeg4_decode_slice(AVCodecContext *avctx, const uint8_t *buf,
                                  uint32_t buf_size)
{
    Mpeg4DecContext *m = avctx->priv_data;
    AVFrame     *frame = m->m.cur_pic.ptr->f;

    /* Rewind the bitstream looking for the VOP start marker */
    while (*(uint32_t *)buf != AV_BE2NE32C(VOP_STARTCODE))
        buf -= 1, buf_size += 1;

    return ff_nvtegra_decode_slice(avctx, frame, buf, buf_size, false);
}

#if CONFIG_MPEG4_NVTEGRA_HWACCEL
const FFHWAccel ff_mpeg4_nvtegra_hwaccel = {
    .p.name         = "mpeg4_nvtegra",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MPEG4,
    .p.pix_fmt      = AV_PIX_FMT_NVTEGRA,
    .start_frame    = &nvtegra_mpeg4_start_frame,
    .end_frame      = &nvtegra_mpeg4_end_frame,
    .decode_slice   = &nvtegra_mpeg4_decode_slice,
    .init           = &nvtegra_mpeg4_decode_init,
    .uninit         = &nvtegra_mpeg4_decode_uninit,
    .frame_params   = &ff_nvtegra_frame_params,
    .priv_data_size = sizeof(NVTegraMPEG4DecodeContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif
