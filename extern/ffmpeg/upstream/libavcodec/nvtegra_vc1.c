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
#include "vc1.h"
#include "decode.h"
#include "nvtegra_decode.h"

#include "libavutil/pixdesc.h"
#include "libavutil/nvtegra_host1x.h"

typedef struct NVTegraVC1DecodeContext {
    FFNVTegraDecodeContext core;

    AVNVTegraMap common_map;
    uint32_t coloc_off, history_off, scratch_off;
    uint32_t history_size, scratch_size;

    bool is_first_slice;

    AVFrame *prev_frame, *next_frame;
} NVTegraVC1DecodeContext;

/* Size (width, height) of a macroblock */
#define MB_SIZE 16

static const uint8_t bitstream_end_sequence[] = {
    0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x00,
};

static int nvtegra_vc1_decode_uninit(AVCodecContext *avctx) {
    NVTegraVC1DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    int err;

    av_log(avctx, AV_LOG_DEBUG, "Deinitializing NVTEGRA VC1 decoder\n");

    err = av_nvtegra_map_destroy(&ctx->common_map);
    if (err < 0)
        return err;

    err = ff_nvtegra_decode_uninit(avctx, &ctx->core);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_vc1_decode_init(AVCodecContext *avctx) {
    NVTegraVC1DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    AVHWDeviceContext      *hw_device_ctx;
    AVNVTegraDeviceContext *device_hwctx;
    uint32_t width_in_mbs, height_in_mbs, num_slices,
             coloc_size, history_size, scratch_size, common_map_size;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Initializing NVTEGRA VC1 decoder\n");

    width_in_mbs  = FFALIGN(avctx->coded_width,  MB_SIZE) / MB_SIZE;
    height_in_mbs = FFALIGN(avctx->coded_height, MB_SIZE) / MB_SIZE;

    num_slices = width_in_mbs * height_in_mbs;

    /* Ignored: histogram map, size 0x400 */
    ctx->core.pic_setup_off     = 0;
    ctx->core.status_off        = FFALIGN(ctx->core.pic_setup_off     + sizeof(nvdec_vc1_pic_s),
                                          AV_NVTEGRA_MAP_ALIGN);
    ctx->core.cmdbuf_off        = FFALIGN(ctx->core.status_off        + sizeof(nvdec_status_s),
                                          AV_NVTEGRA_MAP_ALIGN);
    ctx->core.slice_offsets_off = FFALIGN(ctx->core.cmdbuf_off        + AV_NVTEGRA_MAP_ALIGN,
                                          AV_NVTEGRA_MAP_ALIGN);
    ctx->core.bitstream_off     = FFALIGN(ctx->core.slice_offsets_off + num_slices * sizeof(uint32_t),
                                          AV_NVTEGRA_MAP_ALIGN);
    ctx->core.input_map_size    = FFALIGN(ctx->core.bitstream_off     + ff_nvtegra_decode_pick_bitstream_buffer_size(avctx),
                                          0x1000);

    ctx->core.max_cmdbuf_size    =  ctx->core.slice_offsets_off - ctx->core.cmdbuf_off;
    ctx->core.max_num_slices     = (ctx->core.bitstream_off     - ctx->core.slice_offsets_off) / sizeof(uint32_t);
    ctx->core.max_bitstream_size =  ctx->core.input_map_size    - ctx->core.bitstream_off;

    err = ff_nvtegra_decode_init(avctx, &ctx->core);
    if (err < 0)
        goto fail;

    hw_device_ctx = (AVHWDeviceContext *)ctx->core.hw_device_ref->data;
    device_hwctx  = hw_device_ctx->hwctx;

    coloc_size   = 3 * FFALIGN(width_in_mbs * FFALIGN(height_in_mbs, 2) * 64 - 63, AV_NVTEGRA_MAP_ALIGN);
    history_size = FFALIGN(width_in_mbs, 2) * 0x300;
    scratch_size = 0x400;

    ctx->coloc_off   = 0;
    ctx->history_off = FFALIGN(ctx->coloc_off   + coloc_size,   AV_NVTEGRA_MAP_ALIGN);
    ctx->scratch_off = FFALIGN(ctx->history_off + history_size, AV_NVTEGRA_MAP_ALIGN);
    common_map_size  = FFALIGN(ctx->scratch_off + scratch_size, 0x1000);

    err = av_nvtegra_map_create(&ctx->common_map, &device_hwctx->nvdec_channel, common_map_size, 0x100,
                                NVMAP_HEAP_IOVMM, NVMAP_HANDLE_WRITE_COMBINE);
    if (err < 0)
        goto fail;

    mem = av_nvtegra_map_get_addr(&ctx->common_map);

    memset(mem + ctx->coloc_off,   0, coloc_size);
    memset(mem + ctx->history_off, 0, history_size);
    memset(mem + ctx->scratch_off, 0, scratch_size);

    ctx->history_size = history_size;
    ctx->scratch_size = scratch_size;

    return 0;

fail:
    nvtegra_vc1_decode_uninit(avctx);
    return err;
}

static void nvtegra_vc1_prepare_frame_setup(nvdec_vc1_pic_s *setup, AVCodecContext *avctx,
                                            NVTegraVC1DecodeContext *ctx)
{
    VC1Context     *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    AVFrame    *frame = s->cur_pic.ptr->f;

    /*
     * Notes:
     * - s->current_picture.f->linesize is unconsistently doubled for interlaced content
     *   between I-frames and others, so s->current_pic_ptr is used
     * - a lot of fields in this structure are unused by official software,
     *   here we only set those
     */
    *setup = (nvdec_vc1_pic_s){
        .scratch_pic_buffer_size = ctx->scratch_size,

        .gptimer_timeout_value   = 0, /* Default value */

        .bitstream_offset        = 0,

        .FrameStride             = {
            frame->linesize[0],
            frame->linesize[1],
        },

        .luma_top_offset         = 0,
        .luma_bot_offset         = 0,
        .luma_frame_offset       = 0,
        .chroma_top_offset       = 0,
        .chroma_bot_offset       = 0,
        .chroma_frame_offset     = 0,

        .CodedWidth              = FFALIGN(avctx->coded_width,
                                           (v->profile == PROFILE_ADVANCED) ? 1 : MB_SIZE),
        .CodedHeight             = FFALIGN(avctx->coded_height,
                                           (v->profile == PROFILE_ADVANCED) ? 1 : MB_SIZE),

        .HistBufferSize          = ctx->history_size / 256,

        .loopfilter              = s->loop_filter,

        .output_memory_layout    = 0, /* NV12 */
        .ref_memory_layout       = {
            0, 0, /* NV12 */
        },

        .fastuvmc                = v->fastuvmc,

        .FrameWidth              = FFALIGN(frame->width,
                                           (v->profile == PROFILE_ADVANCED) ? 1 : MB_SIZE),
        .FrameHeight             = FFALIGN(frame->height,
                                           (v->profile == PROFILE_ADVANCED) ? 1 : MB_SIZE),

        .profile                 = (v->profile != PROFILE_ADVANCED) ? 1 : 2,

        .postprocflag            = v->postprocflag,
        .pulldown                = v->broadcast,
        .interlace               = v->interlace,

        .tfcntrflag              = v->tfcntrflag,
        .finterpflag             = v->finterpflag,

        .tileFormat              = 0, /* TBL */

        .psf                     = v->psf,

        .multires                = v->multires,
        .syncmarker              = v->resync_marker,
        .rangered                = v->rangered,
        .maxbframes              = s->max_b_frames,
        .panscan_flag            = v->panscanflag,
        .dquant                  = v->dquant,
        .refdist_flag            = v->refdist_flag,
        .quantizer               = v->quantizer_mode,
        .overlap                 = v->overlap,
        .vstransform             = v->vstransform,
        .extended_mv             = v->extended_mv,
        .extended_dmv            = v->extended_dmv,
    };

    if (v->profile == PROFILE_ADVANCED) {
        setup->displayPara.enableTFOutput = 1;
        setup->displayPara.VC1MapYFlag    = v->range_mapy_flag;
        setup->displayPara.MapYValue      = v->range_mapy;
        setup->displayPara.VC1MapUVFlag   = v->range_mapuv_flag;
        setup->displayPara.MapUVValue     = v->range_mapuv;
    } else if (v->rangered && v->rangeredfrm) {
        setup->displayPara.enableTFOutput = 1;
        setup->displayPara.VC1MapYFlag    = 1;
        setup->displayPara.MapYValue      = 7;
        setup->displayPara.VC1MapUVFlag   = 1;
        setup->displayPara.MapUVValue     = 7;
    }

    if (v->range_mapy_flag || v->range_mapuv_flag) {
        setup->displayPara.OutputBottom[0] = 0;
        setup->displayPara.OutputBottom[1] = 0;
        setup->displayPara.OutputStructure = v->interlace & 1;
        setup->displayPara.OutStride       = frame->linesize[0] & 0xff;
    }
}

static int nvtegra_vc1_prepare_cmdbuf(AVNVTegraCmdbuf *cmdbuf, VC1Context *v, NVTegraVC1DecodeContext *ctx,
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
                          AV_NVTEGRA_ENUM(NVC5B0_SET_APPLICATION_ID, ID, VC1));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_CONTROL_PARAMS,
                          AV_NVTEGRA_ENUM (NVC5B0_SET_CONTROL_PARAMS, CODEC_TYPE,     VC1) |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, ERR_CONCEAL_ON, 1)   |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, GPTIMER_ON,     1));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_PICTURE_INDEX,
                          AV_NVTEGRA_VALUE(NVC5B0_SET_PICTURE_INDEX, INDEX, ctx->core.frame_idx));

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_DRV_PIC_SETUP_OFFSET,
                          input_map,        ctx->core.pic_setup_off,     NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_IN_BUF_BASE_OFFSET,
                          input_map,        ctx->core.bitstream_off,     NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_SLICE_OFFSETS_BUF_OFFSET,
                          input_map,        ctx->core.slice_offsets_off, NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_NVDEC_STATUS_OFFSET,
                          input_map,        ctx->core.status_off,        NVHOST_RELOC_TYPE_DEFAULT);

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_COLOC_DATA_OFFSET,
                          &ctx->common_map, ctx->coloc_off,              NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_HISTORY_OFFSET,
                          &ctx->common_map, ctx->history_off,            NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PIC_SCRATCH_BUF_OFFSET,
                          &ctx->common_map, ctx->scratch_off,            NVHOST_RELOC_TYPE_DEFAULT);

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

    /*
     * TODO: Bind a surface to the postproc output if we need range remapping
    if (((v->profile != PROFILE_ADVANCED) && ((v->rangered != 0) || (v->rangeredfrm != 0))) ||
            ((v->range_mapy_flag != 0) || (v->range_mapuv_flag != 0))) {
        AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_DISPLAY_BUF_LUMA_OFFSET,
                              &output.luma, 0, NVHOST_RELOC_TYPE_DEFAULT);
        AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_DISPLAY_BUF_CHROMA_OFFSET,
                              &output.chroma, 0, NVHOST_RELOC_TYPE_DEFAULT);
    }
     */

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_EXECUTE,
                          AV_NVTEGRA_ENUM(NVC5B0_EXECUTE, AWAKEN, ENABLE));

    err = av_nvtegra_cmdbuf_end(cmdbuf);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_vc1_start_frame(AVCodecContext *avctx, const uint8_t *buf, uint32_t buf_size) {
    VC1Context                *v = avctx->priv_data;
    MpegEncContext            *s = &v->s;
    AVFrame               *frame = s->cur_pic.ptr->f;
    FrameDecodeData         *fdd = (FrameDecodeData *)frame->private_ref->data;
    NVTegraVC1DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    FFNVTegraDecodeFrame *tf;
    AVNVTegraMap *input_map;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Starting VC1-NVTEGRA frame with pixel format %s\n",
           av_get_pix_fmt_name(avctx->sw_pix_fmt));

    ctx->is_first_slice = true;

    err = ff_nvtegra_start_frame(avctx, frame, &ctx->core);
    if (err < 0)
        return err;

    tf = fdd->hwaccel_priv;
    input_map = (AVNVTegraMap *)tf->input_map_ref->data;
    mem = av_nvtegra_map_get_addr(input_map);

    nvtegra_vc1_prepare_frame_setup((nvdec_vc1_pic_s *)(mem + ctx->core.pic_setup_off), avctx, ctx);

    ctx->prev_frame = ff_nvtegra_safe_get_ref(s->last_pic.ptr ? s->last_pic.ptr->f : frame, frame);
    ctx->next_frame = ff_nvtegra_safe_get_ref(s->next_pic.ptr ? s->next_pic.ptr->f : frame, frame);

    return 0;
}

static int nvtegra_vc1_end_frame(AVCodecContext *avctx) {
    VC1Context                *v = avctx->priv_data;
    NVTegraVC1DecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    AVFrame               *frame = v->s.cur_pic.ptr->f;
    FrameDecodeData         *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame     *tf = fdd->hwaccel_priv;

    nvdec_vc1_pic_s *setup;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Ending VC1-NVTEGRA frame with %u slices -> %u bytes\n",
           ctx->core.num_slices, ctx->core.bitstream_len);

    if (!tf || !ctx->core.num_slices)
        return 0;

    mem = av_nvtegra_map_get_addr((AVNVTegraMap *)tf->input_map_ref->data);

    setup = (nvdec_vc1_pic_s *)(mem + ctx->core.pic_setup_off);
    setup->stream_len  = ctx->core.bitstream_len + sizeof(bitstream_end_sequence);
    setup->slice_count = ctx->core.num_slices;

    err = nvtegra_vc1_prepare_cmdbuf(&ctx->core.cmdbuf, v, ctx, frame,
                                     ctx->prev_frame, ctx->next_frame);
    if (err < 0)
        return err;

    return ff_nvtegra_end_frame(avctx, frame, &ctx->core, bitstream_end_sequence,
                                sizeof(bitstream_end_sequence));
}

static int nvtegra_vc1_decode_slice(AVCodecContext *avctx, const uint8_t *buf,
                                    uint32_t buf_size)
{
    VC1Context                *v = avctx->priv_data;
    NVTegraVC1DecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    AVFrame               *frame = v->s.cur_pic.ptr->f;
    FrameDecodeData         *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame     *tf = fdd->hwaccel_priv;
    AVNVTegraMap      *input_map = (AVNVTegraMap *)tf->input_map_ref->data;

    nvdec_vc1_pic_s *setup;
    uint8_t *mem;
    enum VC1Code startcode;

    mem = av_nvtegra_map_get_addr(input_map);

    setup = (nvdec_vc1_pic_s *)(mem + ctx->core.pic_setup_off);

    if (ctx->is_first_slice) {
        startcode = VC1_CODE_FRAME;

        if (v->profile == PROFILE_ADVANCED &&
                v->fcm == ILACE_FIELD && v->second_field)
            startcode = VC1_CODE_FIELD;

        /*
         * Skip a dword if the bitstream already contains the startcode
         * We could probably just not insert our startcode but this is what official code does
         */
        if ((buf_size >= 4) && (AV_RB32(buf) == startcode))
            setup->bitstream_offset = 1;

        AV_WB32(mem + ctx->core.bitstream_off + ctx->core.bitstream_len, startcode);
        ctx->core.bitstream_len += 4;
        ctx->is_first_slice = false;
    }

    return ff_nvtegra_decode_slice(avctx, frame, buf, buf_size, false);
}

#if CONFIG_VC1_NVTEGRA_HWACCEL
const FFHWAccel ff_vc1_nvtegra_hwaccel = {
    .p.name         = "vc1_nvtegra",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VC1,
    .p.pix_fmt      = AV_PIX_FMT_NVTEGRA,
    .start_frame    = &nvtegra_vc1_start_frame,
    .end_frame      = &nvtegra_vc1_end_frame,
    .decode_slice   = &nvtegra_vc1_decode_slice,
    .init           = &nvtegra_vc1_decode_init,
    .uninit         = &nvtegra_vc1_decode_uninit,
    .frame_params   = &ff_nvtegra_frame_params,
    .priv_data_size = sizeof(NVTegraVC1DecodeContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif

#if CONFIG_WMV3_NVTEGRA_HWACCEL
const FFHWAccel ff_wmv3_nvtegra_hwaccel = {
    .p.name         = "wmv3_nvtegra",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV3,
    .p.pix_fmt      = AV_PIX_FMT_NVTEGRA,
    .start_frame    = &nvtegra_vc1_start_frame,
    .end_frame      = &nvtegra_vc1_end_frame,
    .decode_slice   = &nvtegra_vc1_decode_slice,
    .init           = &nvtegra_vc1_decode_init,
    .uninit         = &nvtegra_vc1_decode_uninit,
    .frame_params   = &ff_nvtegra_frame_params,
    .priv_data_size = sizeof(NVTegraVC1DecodeContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif
