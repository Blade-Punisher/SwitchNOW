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
#include "mpegvideo.h"
#include "mpegutils.h"
#include "decode.h"
#include "nvtegra_decode.h"

#include "libavutil/pixdesc.h"
#include "libavutil/nvtegra_host1x.h"

typedef struct NVTegraMPEG12DecodeContext {
    FFNVTegraDecodeContext core;

    AVFrame *prev_frame, *next_frame;
} NVTegraMPEG12DecodeContext;

/* Size (width, height) of a macroblock */
#define MB_SIZE 16

static const uint8_t bitstream_end_sequence[16] = {
    0x00, 0x00, 0x01, 0xb7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xb7, 0x00, 0x00, 0x00, 0x00,
};

static int nvtegra_mpeg12_decode_uninit(AVCodecContext *avctx) {
    NVTegraMPEG12DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    int err;

    av_log(avctx, AV_LOG_DEBUG, "Deinitializing NVTEGRA MPEG12 decoder\n");

    err = ff_nvtegra_decode_uninit(avctx, &ctx->core);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_mpeg12_decode_init(AVCodecContext *avctx) {
    NVTegraMPEG12DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    uint32_t num_slices;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Initializing NVTEGRA MPEG12 decoder\n");

    num_slices = (FFALIGN(avctx->coded_width,  MB_SIZE) / MB_SIZE) *
                 (FFALIGN(avctx->coded_height, MB_SIZE) / MB_SIZE);
    num_slices = FFMIN(num_slices, 8160);

    /* Ignored: histogram map, size 0x400 */
    ctx->core.pic_setup_off     = 0;
    ctx->core.status_off        = FFALIGN(ctx->core.pic_setup_off     + sizeof(nvdec_mpeg2_pic_s),
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

    return 0;

fail:
    nvtegra_mpeg12_decode_uninit(avctx);
    return err;
}

static void nvtegra_mpeg12_prepare_frame_setup(nvdec_mpeg2_pic_s *setup, MpegEncContext *s,
                                               NVTegraMPEG12DecodeContext *ctx)
{
    *setup = (nvdec_mpeg2_pic_s){
        .gptimer_timeout_value      = 0, /* Default value */

        .FrameWidth                 = FFALIGN(s->width,  MB_SIZE),
        .FrameHeight                = FFALIGN(s->height, MB_SIZE),

        .picture_structure          = s->picture_structure,
        .picture_coding_type        = s->pict_type,
        .intra_dc_precision         = s->intra_dc_precision,
        .frame_pred_frame_dct       = s->frame_pred_frame_dct,
        .concealment_motion_vectors = s->concealment_motion_vectors,
        .intra_vlc_format           = s->intra_vlc_format,

        .tileFormat                 = 0, /* TBL */
        .gob_height                 = 0, /* GOB_2 */

        .f_code                     = {
            s->mpeg_f_code[0][0], s->mpeg_f_code[0][1],
            s->mpeg_f_code[1][0], s->mpeg_f_code[1][1],
        },

        .PicWidthInMbs              = FFALIGN(s->width,  MB_SIZE) / MB_SIZE,
        .FrameHeightInMbs           = FFALIGN(s->height, MB_SIZE) / MB_SIZE,
        .pitch_luma                 = s->cur_pic.ptr->f->linesize[0],
        .pitch_chroma               = s->cur_pic.ptr->f->linesize[1],
        .luma_top_offset            = 0,
        .luma_bot_offset            = 0,
        .luma_frame_offset          = 0,
        .chroma_top_offset          = 0,
        .chroma_bot_offset          = 0,
        .chroma_frame_offset        = 0,
        .alternate_scan             = s->alternate_scan,
        .secondfield                = s->picture_structure != PICT_FRAME && !s->first_field,
        .rounding_type              = 0,
        .q_scale_type               = s->q_scale_type,
        .top_field_first            = s->top_field_first,
        .full_pel_fwd_vector        = (s->codec_id != AV_CODEC_ID_MPEG2VIDEO) ? s->full_pel[0] : 0,
        .full_pel_bwd_vector        = (s->codec_id != AV_CODEC_ID_MPEG2VIDEO) ? s->full_pel[1] : 0,
        .output_memory_layout       = 0, /* NV12 */
        .ref_memory_layout          = { 0, 0 }, /* NV12 */
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(setup->quant_mat_8x8intra); ++i) {
        setup->quant_mat_8x8intra   [i] = (NvU8)s->intra_matrix[i];
        setup->quant_mat_8x8nonintra[i] = (NvU8)s->inter_matrix[i];
    }
}

static int nvtegra_mpeg12_prepare_cmdbuf(AVNVTegraCmdbuf *cmdbuf, MpegEncContext *s, NVTegraMPEG12DecodeContext *ctx,
                                         AVFrame *current_frame, AVFrame *prev_frame, AVFrame *next_frame)
{
    FrameDecodeData     *fdd = (FrameDecodeData *)current_frame->private_ref->data;
    FFNVTegraDecodeFrame *tf = fdd->hwaccel_priv;
    AVNVTegraMap  *input_map = (AVNVTegraMap *)tf->input_map_ref->data;

    int err, codec_id;

    err = av_nvtegra_cmdbuf_begin(cmdbuf, HOST1X_CLASS_NVDEC);
    if (err < 0)
        return err;

    switch (s->codec_id) {
        case AV_CODEC_ID_MPEG1VIDEO:
            codec_id = NVC5B0_SET_CONTROL_PARAMS_CODEC_TYPE_MPEG1;
            break;
        case AV_CODEC_ID_MPEG2VIDEO:
            codec_id = NVC5B0_SET_CONTROL_PARAMS_CODEC_TYPE_MPEG2;
            break;
        default:
            return AVERROR(EINVAL);
    }

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_APPLICATION_ID,
                          AV_NVTEGRA_ENUM(NVC5B0_SET_APPLICATION_ID, ID, MPEG12));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_CONTROL_PARAMS, codec_id                    |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, ERR_CONCEAL_ON, 1) |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, GPTIMER_ON,     1));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_PICTURE_INDEX,
                          AV_NVTEGRA_VALUE(NVC5B0_SET_PICTURE_INDEX, INDEX, ctx->core.frame_idx));

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_DRV_PIC_SETUP_OFFSET,
                          input_map, ctx->core.pic_setup_off,     NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_IN_BUF_BASE_OFFSET,
                          input_map, ctx->core.bitstream_off,     NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_SLICE_OFFSETS_BUF_OFFSET,
                          input_map, ctx->core.slice_offsets_off, NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_NVDEC_STATUS_OFFSET,
                          input_map, ctx->core.status_off,        NVHOST_RELOC_TYPE_DEFAULT);

#define PUSH_FRAME(fr, offset) ({                                                           \
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PICTURE_LUMA_OFFSET0   + offset * 4,           \
                          av_nvtegra_frame_get_fbuf_map(fr), 0, NVHOST_RELOC_TYPE_DEFAULT); \
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PICTURE_CHROMA_OFFSET0 + offset * 4,           \
                          av_nvtegra_frame_get_fbuf_map(fr), fr->data[1] - fr->data[0],     \
                          NVHOST_RELOC_TYPE_DEFAULT);                                       \
})

    PUSH_FRAME(current_frame, 0);
    PUSH_FRAME(prev_frame,    1);
    PUSH_FRAME(next_frame,    2);

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_EXECUTE,
                          AV_NVTEGRA_ENUM(NVC5B0_EXECUTE, AWAKEN, ENABLE));

    err = av_nvtegra_cmdbuf_end(cmdbuf);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_mpeg12_start_frame(AVCodecContext *avctx, const uint8_t *buf, uint32_t buf_size) {
    MpegEncContext               *s = avctx->priv_data;
    AVFrame                  *frame = s->cur_pic.ptr->f;
    FrameDecodeData            *fdd = (FrameDecodeData *)frame->private_ref->data;
    NVTegraMPEG12DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    FFNVTegraDecodeFrame *tf;
    AVNVTegraMap *input_map;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Starting MPEG12-NVTEGRA frame with pixel format %s\n",
           av_get_pix_fmt_name(avctx->sw_pix_fmt));

    err = ff_nvtegra_start_frame(avctx, frame, &ctx->core);
    if (err < 0)
        return err;

    tf = fdd->hwaccel_priv;
    input_map = (AVNVTegraMap *)tf->input_map_ref->data;
    mem = av_nvtegra_map_get_addr(input_map);

    nvtegra_mpeg12_prepare_frame_setup((nvdec_mpeg2_pic_s *)(mem + ctx->core.pic_setup_off), s, ctx);

    ctx->prev_frame = (s->pict_type != AV_PICTURE_TYPE_I && s->last_pic.ptr) ? s->last_pic.ptr->f : frame;
    ctx->next_frame = (s->pict_type == AV_PICTURE_TYPE_B && s->next_pic.ptr) ? s->next_pic.ptr->f : frame;

    return 0;
}

static int nvtegra_mpeg12_end_frame(AVCodecContext *avctx) {
    MpegEncContext               *s = avctx->priv_data;
    NVTegraMPEG12DecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    AVFrame                  *frame = s->cur_pic.ptr->f;
    FrameDecodeData            *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame        *tf = fdd->hwaccel_priv;

    nvdec_mpeg2_pic_s *setup;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Ending MPEG12-NVTEGRA frame with %u slices -> %u bytes\n",
           ctx->core.num_slices, ctx->core.bitstream_len);

    if (!tf || !ctx->core.num_slices)
        return 0;

    mem = av_nvtegra_map_get_addr((AVNVTegraMap *)tf->input_map_ref->data);

    setup = (nvdec_mpeg2_pic_s *)(mem + ctx->core.pic_setup_off);
    setup->stream_len  = ctx->core.bitstream_len + sizeof(bitstream_end_sequence);
    setup->slice_count = ctx->core.num_slices;

    err = nvtegra_mpeg12_prepare_cmdbuf(&ctx->core.cmdbuf, s, ctx, frame,
                                        ctx->prev_frame, ctx->next_frame);
    if (err < 0)
        return err;

    return ff_nvtegra_end_frame(avctx, frame, &ctx->core, bitstream_end_sequence,
                                sizeof(bitstream_end_sequence));
}

static int nvtegra_mpeg12_decode_slice(AVCodecContext *avctx, const uint8_t *buf, uint32_t buf_size) {
    MpegEncContext *s = avctx->priv_data;
    AVFrame    *frame = s->cur_pic.ptr->f;

    return ff_nvtegra_decode_slice(avctx, frame, buf, buf_size, false);
}

#if CONFIG_MPEG1_NVTEGRA_HWACCEL
const FFHWAccel ff_mpeg1_nvtegra_hwaccel = {
    .p.name         = "mpeg1_nvtegra",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MPEG1VIDEO,
    .p.pix_fmt      = AV_PIX_FMT_NVTEGRA,
    .start_frame    = &nvtegra_mpeg12_start_frame,
    .end_frame      = &nvtegra_mpeg12_end_frame,
    .decode_slice   = &nvtegra_mpeg12_decode_slice,
    .init           = &nvtegra_mpeg12_decode_init,
    .uninit         = &nvtegra_mpeg12_decode_uninit,
    .frame_params   = &ff_nvtegra_frame_params,
    .priv_data_size = sizeof(NVTegraMPEG12DecodeContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif

#if CONFIG_MPEG2_NVTEGRA_HWACCEL
const FFHWAccel ff_mpeg2_nvtegra_hwaccel = {
    .p.name         = "mpeg2_nvtegra",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MPEG2VIDEO,
    .p.pix_fmt      = AV_PIX_FMT_NVTEGRA,
    .start_frame    = &nvtegra_mpeg12_start_frame,
    .end_frame      = &nvtegra_mpeg12_end_frame,
    .decode_slice   = &nvtegra_mpeg12_decode_slice,
    .init           = &nvtegra_mpeg12_decode_init,
    .uninit         = &nvtegra_mpeg12_decode_uninit,
    .frame_params   = &ff_nvtegra_frame_params,
    .priv_data_size = sizeof(NVTegraMPEG12DecodeContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif
