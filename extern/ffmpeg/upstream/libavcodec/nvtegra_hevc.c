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
#include <string.h>

#include "avcodec.h"
#include "hwaccel_internal.h"
#include "internal.h"
#include "hwconfig.h"
#include "hevc/hevcdec.h"
#include "hevc/data.h"
#include "decode.h"
#include "nvtegra_decode.h"

#include "libavutil/pixdesc.h"
#include "libavutil/nvtegra_host1x.h"

typedef struct NVTegraHEVCFrameData {
    bool initialized;
    uint8_t dpb_idx;
} NVTegraHEVCFrameData;

typedef struct NVTegraHEVCDecodeContext {
    FFNVTegraDecodeContext core;

    AVNVTegraMap common_map;
    uint32_t tile_sizes_off, scaling_list_off,
             coloc_off, filter_off;

    unsigned int colmv_size, sao_offset, bsd_offset;
    uint8_t pattern_id;

    HEVCFrame *refs[16], *scratch_ref;
    uint32_t refs_mask;
} NVTegraHEVCDecodeContext;

/* Size (width, height) of a macroblock */
#define MB_SIZE 16

/* Maximum size (width, height) of a coding tree unit */
#define CTU_SIZE 64

#define FILTER_SIZE 480
#define SAO_SIZE    3840
#define BSD_SIZE    60

static int nvtegra_hevc_decode_uninit(AVCodecContext *avctx) {
    NVTegraHEVCDecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    int err;

    av_log(avctx, AV_LOG_DEBUG, "Deinitializing NVTEGRA HEVC decoder\n");

    err = av_nvtegra_map_destroy(&ctx->common_map);
    if (err < 0)
        return err;

    err = ff_nvtegra_decode_uninit(avctx, &ctx->core);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_hevc_decode_init(AVCodecContext *avctx) {
    NVTegraHEVCDecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    AVHWDeviceContext      *hw_device_ctx;
    AVNVTegraDeviceContext *device_hwctx;
    uint32_t aligned_width, aligned_height,
             coloc_size, filter_buffer_size, common_map_size;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Initializing NVTEGRA HEVC decoder\n");

    ctx->core.pic_setup_off  = 0;
    ctx->core.status_off     = FFALIGN(ctx->core.pic_setup_off + sizeof(nvdec_hevc_pic_s),
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.cmdbuf_off     = FFALIGN(ctx->core.status_off    + sizeof(nvdec_status_s),
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->tile_sizes_off      = FFALIGN(ctx->core.cmdbuf_off    + 3*AV_NVTEGRA_MAP_ALIGN,
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->scaling_list_off    = FFALIGN(ctx->tile_sizes_off     + 0x900,
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.bitstream_off  = FFALIGN(ctx->scaling_list_off   + 0x400,
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.input_map_size = FFALIGN(ctx->core.bitstream_off + ff_nvtegra_decode_pick_bitstream_buffer_size(avctx),
                                       0x1000);

    ctx->core.max_cmdbuf_size    = ctx->tile_sizes_off      - ctx->core.cmdbuf_off;
    ctx->core.max_bitstream_size = ctx->core.input_map_size - ctx->core.bitstream_off;

    err = ff_nvtegra_decode_init(avctx, &ctx->core);
    if (err < 0)
        goto fail;

    hw_device_ctx = (AVHWDeviceContext *)ctx->core.hw_device_ref->data;
    device_hwctx  = hw_device_ctx->hwctx;

    aligned_width      = FFALIGN(avctx->coded_width,  CTU_SIZE);
    aligned_height     = FFALIGN(avctx->coded_height, CTU_SIZE);
    coloc_size         = (aligned_width * aligned_height) + (aligned_width * aligned_height / MB_SIZE);
    filter_buffer_size = (FILTER_SIZE + SAO_SIZE + BSD_SIZE) * aligned_height;

    ctx->coloc_off  = 0;
    ctx->filter_off = FFALIGN(ctx->coloc_off  + coloc_size,         AV_NVTEGRA_MAP_ALIGN);
    common_map_size = FFALIGN(ctx->filter_off + filter_buffer_size, 0x1000);

    err = av_nvtegra_map_create(&ctx->common_map, &device_hwctx->nvdec_channel, common_map_size, 0x100,
                                NVMAP_HEAP_IOVMM, NVMAP_HANDLE_WRITE_COMBINE);
    if (err < 0)
        goto fail;

    ctx->colmv_size = aligned_width * aligned_height / 16;
    ctx->sao_offset =  FILTER_SIZE             * aligned_height;
    ctx->bsd_offset = (FILTER_SIZE + SAO_SIZE) * aligned_height;

    return 0;

fail:
    nvtegra_hevc_decode_uninit(avctx);
    return err;
}

static void nvtegra_hevc_set_scaling_list(nvdec_hevc_scaling_list_s *list, HEVCContext *s) {
    const ScalingList *sl = s->pps->scaling_list_data_present_flag ?
                            &s->pps->scaling_list : &s->pps->sps->scaling_list;

    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(list->ScalingListDCCoeff16x16); ++i)
        list->ScalingListDCCoeff16x16[i] = sl->sl_dc[0][i];
    for (i = 0; i < FF_ARRAY_ELEMS(list->ScalingListDCCoeff32x32); ++i)
        list->ScalingListDCCoeff32x32[i] = sl->sl_dc[1][i * 3];

    for (i = 0; i < 6; ++i)
        memcpy(list->ScalingList4x4[i],   sl->sl[0][i], 16);
    for (i = 0; i < 6; ++i)
        memcpy(list->ScalingList8x8[i],   sl->sl[1][i], 64);
    for (i = 0; i < 6; ++i)
        memcpy(list->ScalingList16x16[i], sl->sl[2][i], 64);
    memcpy(list->ScalingList32x32[0], sl->sl[3][0], 64);
    memcpy(list->ScalingList32x32[1], sl->sl[3][3], 64);
}

static void nvtegra_hevc_set_tile_sizes(uint16_t *sizes, HEVCContext *s) {
    const HEVCPPS *pps = s->pps;
    const HEVCSPS *sps = pps->sps;

    int i, j, sum;

    uint16_t *tile_thing = sizes + 0x380;
    if (pps->uniform_spacing_flag) {
        for (i = 0; i < pps->num_tile_columns; ++i)
            *tile_thing++ = (i + 1) * sps->ctb_width  / pps->num_tile_columns <<
                (sps->log2_diff_max_min_coding_block_size + sps->log2_min_cb_size - 4);
        for (i = 0; i < pps->num_tile_rows; ++i)
            *tile_thing++ = (i + 1) * sps->ctb_height / pps->num_tile_rows    <<
                (sps->log2_diff_max_min_coding_block_size + sps->log2_min_cb_size - 4);
    } else {
        sum = 0;
        for (i = 0; i < pps->num_tile_columns; ++i)
            *tile_thing++ = (sum += pps->column_width[i]) <<
                (sps->log2_diff_max_min_coding_block_size + sps->log2_min_cb_size - 4);
        sum = 0;
        for (i = 0; i < pps->num_tile_rows; ++i)
            *tile_thing++ = (sum += pps->row_height[i])   <<
                (sps->log2_diff_max_min_coding_block_size + sps->log2_min_cb_size - 4);
    }

    for (i = 0; i < pps->num_tile_rows; ++i) {
        for (j = 0; j < pps->num_tile_columns; ++j) {
            sizes[0] = pps->column_width[j];
            sizes[1] = pps->row_height  [i];
            sizes += 2;
        }
    }
}

static enum RPSType find_ref_rps_type(HEVCContext *s, HEVCFrame *f) {
    int i;

#define CHECK_SET(set) ({                       \
    for (i = 0; i < s->rps[set].nb_refs; ++i) { \
        if (s->rps[set].ref[i] == f)            \
            return set;                         \
    }                                           \
})

    CHECK_SET(ST_CURR_BEF);
    CHECK_SET(ST_CURR_AFT);
    CHECK_SET(ST_FOLL);
    CHECK_SET(LT_CURR);
    CHECK_SET(LT_FOLL);

    return -1;
}

static inline int find_slot(uint32_t *mask) {
    int slot = ff_ctz(~*mask);
    *mask |= (1 << slot);
    return slot;
}

static void nvtegra_hevc_prepare_frame_setup(nvdec_hevc_pic_s *setup, AVCodecContext *avctx,
                                             AVFrame *frame, NVTegraHEVCDecodeContext *ctx)
{
    FrameDecodeData          *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame      *tf = fdd->hwaccel_priv;
    AVNVTegraMap       *input_map = (AVNVTegraMap *)tf->input_map_ref->data;
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
    HEVCContext                *s = avctx->priv_data;
    HEVCLayerContext           *l = &s->layers[s->cur_layer];
    SliceHeader               *sh = &s->sh;
    const HEVCPPS            *pps = s->pps;
    const HEVCSPS            *sps = pps->sps;

    HEVCFrame *fr;
    NVTegraHEVCFrameData *fr_priv;
    enum RPSType st;
    uint8_t *mem;
    uint16_t *tile_sizes;
    int output_mode, cur_frame, scratch_ref_diff_poc, i, j;
    int8_t rps_stcurrbef[8], rps_stcurraft[8], rps_ltcurr[8];

    mem = av_nvtegra_map_get_addr(input_map);

    /* Match source color depth regardless of colorspace */
    /* TODO: Dithered down 8-bit post-processing (needs DISPLAY_BUF mappings) */
    if (frames_ctx->sw_format == AV_PIX_FMT_P010 && sps->bit_depth == 10) {
        output_mode = 1;                /* 10-bit bt709 */
    } else {
        if (sps->bit_depth == 8) {
            output_mode = 0;            /* 8-bit bt709 */
        } else {
            switch (avctx->colorspace) {
                default:
                case AVCOL_SPC_BT709:
                    output_mode = 2;    /* 10-bit bt709 truncated to 8-bit */
                    break;
                case AVCOL_SPC_BT2020_CL:
                case AVCOL_SPC_BT2020_NCL:
                    output_mode = 3;    /* 10-bit bt2020 truncated to 8-bit */
                    break;
            }
        }
    }

    *setup = (nvdec_hevc_pic_s){
        .gptimer_timeout_value                       = 0, /* Default value */

        .tileformat                                  = 0, /* TBL */
        .gob_height                                  = 0, /* GOB_2 */

        .sw_start_code_e                             = 1,
        .disp_output_mode                            = output_mode,

        /* Divide by two if we are decoding to a 2bpp surface */
        .framestride                                 = {
            s->cur_frame->f->linesize[0] / ((output_mode == 1) ? 2 : 1),
            s->cur_frame->f->linesize[1] / ((output_mode == 1) ? 2 : 1),
        },

        .colMvBuffersize                             = ctx->colmv_size / 256,
        .HevcSaoBufferOffset                         = ctx->sao_offset / 256,
        .HevcBsdCtrlOffset                           = ctx->bsd_offset / 256,

        .pic_width_in_luma_samples                   = sps->width,
        .pic_height_in_luma_samples                  = sps->height,

        .chroma_format_idc                           = 1, /* 4:2:0 */
        .bit_depth_luma                              = sps->bit_depth,
        .bit_depth_chroma                            = sps->bit_depth,
        .log2_min_luma_coding_block_size             = sps->log2_min_cb_size,
        .log2_max_luma_coding_block_size             = sps->log2_diff_max_min_coding_block_size + sps->log2_min_cb_size,
        .log2_min_transform_block_size               = sps->log2_min_tb_size,
        .log2_max_transform_block_size               = sps->log2_max_trafo_size,

        .max_transform_hierarchy_depth_inter         = sps->max_transform_hierarchy_depth_inter,
        .max_transform_hierarchy_depth_intra         = sps->max_transform_hierarchy_depth_intra,
        .scalingListEnable                           = sps->scaling_list_enabled,
        .amp_enable_flag                             = sps->amp_enabled,
        .sample_adaptive_offset_enabled_flag         = sps->sao_enabled,
        .pcm_enabled_flag                            = sps->pcm_enabled,
        .pcm_sample_bit_depth_luma                   = sps->pcm_enabled ? sps->pcm.bit_depth            : 0,
        .pcm_sample_bit_depth_chroma                 = sps->pcm_enabled ? sps->pcm.bit_depth_chroma     : 0,
        .log2_min_pcm_luma_coding_block_size         = sps->pcm_enabled ? sps->pcm.log2_min_pcm_cb_size : 0,
        .log2_max_pcm_luma_coding_block_size         = sps->pcm_enabled ? sps->pcm.log2_max_pcm_cb_size : 0,
        .pcm_loop_filter_disabled_flag               = sps->pcm_loop_filter_disabled,
        .sps_temporal_mvp_enabled_flag               = sps->temporal_mvp_enabled,
        .strong_intra_smoothing_enabled_flag         = sps->strong_intra_smoothing_enabled,

        .dependent_slice_segments_enabled_flag       = pps->dependent_slice_segments_enabled_flag,
        .output_flag_present_flag                    = pps->output_flag_present_flag,
        .num_extra_slice_header_bits                 = pps->num_extra_slice_header_bits,
        .sign_data_hiding_enabled_flag               = pps->sign_data_hiding_flag,
        .cabac_init_present_flag                     = pps->cabac_init_present_flag,
        .num_ref_idx_l0_default_active               = pps->num_ref_idx_l0_default_active,
        .num_ref_idx_l1_default_active               = pps->num_ref_idx_l1_default_active,
        .init_qp                                     = pps->pic_init_qp_minus26 + 26 + (sps->bit_depth - 8) * 6,
        .constrained_intra_pred_flag                 = pps->constrained_intra_pred_flag,
        .transform_skip_enabled_flag                 = pps->transform_skip_enabled_flag,
        .cu_qp_delta_enabled_flag                    = pps->cu_qp_delta_enabled_flag,
        .diff_cu_qp_delta_depth                      = pps->diff_cu_qp_delta_depth,

        .pps_cb_qp_offset                            = pps->cb_qp_offset,
        .pps_cr_qp_offset                            = pps->cr_qp_offset,
        .pps_beta_offset                             = pps->beta_offset,
        .pps_tc_offset                               = pps->tc_offset,
        .pps_slice_chroma_qp_offsets_present_flag    = pps->pic_slice_level_chroma_qp_offsets_present_flag,
        .weighted_pred_flag                          = pps->weighted_pred_flag,
        .weighted_bipred_flag                        = pps->weighted_bipred_flag,
        .transquant_bypass_enabled_flag              = pps->transquant_bypass_enable_flag,
        .tiles_enabled_flag                          = pps->tiles_enabled_flag,
        .entropy_coding_sync_enabled_flag            = pps->entropy_coding_sync_enabled_flag,
        .num_tile_columns                            = pps->tiles_enabled_flag ? pps->num_tile_columns : 0,
        .num_tile_rows                               = pps->tiles_enabled_flag ? pps->num_tile_rows    : 0,
        .loop_filter_across_tiles_enabled_flag       = pps->tiles_enabled_flag ? pps->loop_filter_across_tiles_enabled_flag : 0,
        .loop_filter_across_slices_enabled_flag      = pps->seq_loop_filter_across_slices_enabled_flag,
        .deblocking_filter_control_present_flag      = pps->deblocking_filter_control_present_flag,
        .deblocking_filter_override_enabled_flag     = pps->deblocking_filter_override_enabled_flag,
        .pps_deblocking_filter_disabled_flag         = pps->disable_dbf,
        .lists_modification_present_flag             = pps->lists_modification_present_flag,
        .log2_parallel_merge_level                   = pps->log2_parallel_merge_level,
        .slice_segment_header_extension_present_flag = pps->slice_header_extension_present_flag,

        .num_ref_frames                              = ff_hevc_frame_nb_refs(sh, pps, s->cur_layer),

        .IDR_picture_flag                            = IS_IDR(s),
        .RAP_picture_flag                            = IS_IRAP(s),
        .pattern_id                                  = ((output_mode == 0) || (output_mode == 1)) ? 2 : ctx->pattern_id, /* Disable/enable dithering */
        .sw_hdr_skip_length                          = sh->nvidia_skip_length,

        /*
         * Ignored in official code
        .separate_colour_plane_flag                  = sps->separate_colour_plane_flag,
        .log2_max_pic_order_cnt_lsb_minus4           = sps->log2_max_poc_lsb - 4,
        .num_short_term_ref_pic_sets                 = sps->nb_st_rps,
        .num_long_term_ref_pics_sps                  = sps->num_long_term_ref_pics_sps,
        .num_delta_pocs_of_rps_idx                   = s->sh.short_term_rps ? s->sh.short_term_rps->rps_idx_num_delta_pocs : 0,
        .long_term_ref_pics_present_flag             = sps->long_term_ref_pics_present_flag,
        .num_bits_short_term_ref_pics_in_slice       = sh->short_term_ref_pic_set_size;
        */
    };

    /*
     * Decoded frames need to be allocated an index that represents its position
     * in the data pointers array (pushed to the cmdbuf) and in the metadata
     * sent to the hardware.
     * This index must remain fixed until the frame is dropped from the DPB.
     */

    /* Build ordered reflist from the DPB */
    for (i = 0; i < FF_ARRAY_ELEMS(l->DPB); ++i) {
        fr      = &l->DPB[i];
        fr_priv = fr->hwaccel_picture_private;

        if ((fr->flags & (HEVC_FRAME_FLAG_LONG_REF | HEVC_FRAME_FLAG_SHORT_REF)) &&
            (fr != s->cur_frame) && fr_priv->initialized)
        {
            ctx->refs[fr_priv->dpb_idx] = fr;
            ctx->refs_mask |= 1 << fr_priv->dpb_idx;
        }
    }

    /* Try to find a valid reference, or use the current one */
    ctx->scratch_ref = s->cur_frame, scratch_ref_diff_poc = 0;
    for (i = 0; i < FF_ARRAY_ELEMS(ctx->refs); ++i) {
        fr = ctx->refs[i];
        if (!(ctx->refs_mask & (1 << i)) || (fr == s->cur_frame))
            continue;

        st = find_ref_rps_type(s, fr);
        if ((st != ST_CURR_BEF) && (st != ST_CURR_AFT) && (st != LT_CURR))
            continue;

        ctx->scratch_ref     = fr;
        scratch_ref_diff_poc = av_clip_int8(s->cur_frame->poc - fr->poc);
        break;
    }

    /* Add the current frame to our ref list */
    setup->curr_pic_idx = cur_frame = find_slot(&ctx->refs_mask);
    ctx->refs[cur_frame] = s->cur_frame;

    fr_priv = s->cur_frame->hwaccel_picture_private;
    *fr_priv = (NVTegraHEVCFrameData){
        .dpb_idx     = cur_frame,
        .initialized = true,
    };

    /* Fill the POC metadata */
    for (i = 0; i < FF_ARRAY_ELEMS(setup->RefDiffPicOrderCnts); ++i) {
        if (i == cur_frame)
            continue;

        if (ctx->refs_mask & (1 << i)) {
            fr = ctx->refs[i];
            setup->RefDiffPicOrderCnts[i] = av_clip_int8(s->cur_frame->poc - fr->poc);
            setup->longtermflag |= !!(fr->flags & HEVC_FRAME_FLAG_LONG_REF) << (15 - i);
        } else {
            setup->RefDiffPicOrderCnts[i] = scratch_ref_diff_poc;
        }
    }

#define RPS_TO_DPB_IDX(set, array) ({                       \
    for (i = 0; i < s->rps[set].nb_refs; ++i) {             \
        for (j = 0; j < FF_ARRAY_ELEMS(ctx->refs); ++j) {   \
            if (s->rps[set].ref[i] == ctx->refs[j]) {       \
                array[i] = j;                               \
                break;                                      \
            }                                               \
        }                                                   \
    }                                                       \
})

    RPS_TO_DPB_IDX(ST_CURR_BEF, rps_stcurrbef);
    RPS_TO_DPB_IDX(ST_CURR_AFT, rps_stcurraft);
    RPS_TO_DPB_IDX(LT_CURR,     rps_ltcurr);

#define FILL_REFLIST(list, set, array) ({         \
    int len = FFMIN(s->rps[set].nb_refs, 16 - i); \
    memcpy(&setup->list[i], array, len);          \
    i += len;                                     \
})

    /* Fill the RPS metadata */
    if (s->rps[ST_CURR_BEF].nb_refs + s->rps[ST_CURR_AFT].nb_refs + s->rps[LT_CURR].nb_refs) {
        for (i = 0; i < 16;) {
            FILL_REFLIST(initreflistidxl0, ST_CURR_BEF, rps_stcurrbef);
            FILL_REFLIST(initreflistidxl0, ST_CURR_AFT, rps_stcurraft);
            FILL_REFLIST(initreflistidxl0, LT_CURR,     rps_ltcurr);
        }

        for (i = 0; i < 16;) {
            FILL_REFLIST(initreflistidxl1, ST_CURR_AFT, rps_stcurraft);
            FILL_REFLIST(initreflistidxl1, ST_CURR_BEF, rps_stcurrbef);
            FILL_REFLIST(initreflistidxl1, LT_CURR,     rps_ltcurr);
        }
    }

    ctx->pattern_id ^= 1;

    if (sps->scaling_list_enabled)
        nvtegra_hevc_set_scaling_list((nvdec_hevc_scaling_list_s *)(mem + ctx->scaling_list_off), s);

    tile_sizes = (uint16_t *)(mem + ctx->tile_sizes_off);
    if (pps->tiles_enabled_flag) {
        nvtegra_hevc_set_tile_sizes(tile_sizes, s);
    } else {
        tile_sizes[0] = pps->column_width[0];
        tile_sizes[1] = pps->row_height  [0];
    }
}

static int nvtegra_hevc_prepare_cmdbuf(AVNVTegraCmdbuf *cmdbuf, HEVCContext *s,
                                       NVTegraHEVCDecodeContext *ctx, AVFrame *cur_frame)
{
    FrameDecodeData     *fdd = (FrameDecodeData *)cur_frame->private_ref->data;
    FFNVTegraDecodeFrame *tf = fdd->hwaccel_priv;
    AVNVTegraMap  *input_map = (AVNVTegraMap *)tf->input_map_ref->data;

    int i;
    int err;

    err = av_nvtegra_cmdbuf_begin(cmdbuf, HOST1X_CLASS_NVDEC);
    if (err < 0)
        return err;

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_APPLICATION_ID,
                          AV_NVTEGRA_ENUM(NVC5B0_SET_APPLICATION_ID, ID, HEVC));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_CONTROL_PARAMS,
                          AV_NVTEGRA_ENUM (NVC5B0_SET_CONTROL_PARAMS, CODEC_TYPE,     HEVC) |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, ERR_CONCEAL_ON, 1)    |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, GPTIMER_ON,     1));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_PICTURE_INDEX,
                          AV_NVTEGRA_VALUE(NVC5B0_SET_PICTURE_INDEX, INDEX, ctx->core.frame_idx));

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_DRV_PIC_SETUP_OFFSET,
                          input_map,        ctx->core.pic_setup_off, NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_IN_BUF_BASE_OFFSET,
                          input_map,        ctx->core.bitstream_off, NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_NVDEC_STATUS_OFFSET,
                          input_map,        ctx->core.status_off,    NVHOST_RELOC_TYPE_DEFAULT);

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_HEVC_SET_SCALING_LIST_OFFSET,
                          input_map,        ctx->scaling_list_off,   NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_HEVC_SET_TILE_SIZES_OFFSET,
                          input_map,        ctx->tile_sizes_off,     NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_HEVC_SET_FILTER_BUFFER_OFFSET,
                          &ctx->common_map, ctx->filter_off,         NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_COLOC_DATA_OFFSET,
                          &ctx->common_map, ctx->coloc_off,          NVHOST_RELOC_TYPE_DEFAULT);

#define PUSH_FRAME(fr, offset) ({                                                           \
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PICTURE_LUMA_OFFSET0   + offset * 4,           \
                          av_nvtegra_frame_get_fbuf_map(fr), 0, NVHOST_RELOC_TYPE_DEFAULT); \
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PICTURE_CHROMA_OFFSET0 + offset * 4,           \
                          av_nvtegra_frame_get_fbuf_map(fr), fr->data[1] - fr->data[0],     \
                          NVHOST_RELOC_TYPE_DEFAULT);                                       \
})

    for (i = 0; i < FF_ARRAY_ELEMS(ctx->refs); ++i) {
        if (ctx->refs_mask & (1 << i))
            PUSH_FRAME(ctx->refs[i]->f,     i);
        else
            PUSH_FRAME(ctx->scratch_ref->f, i);
    }

    /* TODO: Dithered 8-bit post-processing, binding to DISPLAY_BUF */

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_EXECUTE,
                          AV_NVTEGRA_ENUM(NVC5B0_EXECUTE, AWAKEN, ENABLE));

    err = av_nvtegra_cmdbuf_end(cmdbuf);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_hevc_start_frame(AVCodecContext *avctx, const uint8_t *buf, uint32_t buf_size) {
    HEVCContext                *s = avctx->priv_data;
    AVFrame                *frame = s->cur_frame->f;
    FrameDecodeData          *fdd = (FrameDecodeData *)frame->private_ref->data;
    NVTegraHEVCDecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    FFNVTegraDecodeFrame *tf;
    AVNVTegraMap *input_map;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Starting HEVC-NVTEGRA frame with pixel format %s\n",
           av_get_pix_fmt_name(avctx->sw_pix_fmt));

    err = ff_nvtegra_start_frame(avctx, frame, &ctx->core);
    if (err < 0)
        return err;

    memset(ctx->refs, 0, sizeof(ctx->refs));
    ctx->refs_mask = 0;

    tf = fdd->hwaccel_priv;
    input_map = (AVNVTegraMap *)tf->input_map_ref->data;
    mem = av_nvtegra_map_get_addr(input_map);

    nvtegra_hevc_prepare_frame_setup((nvdec_hevc_pic_s *)(mem + ctx->core.pic_setup_off),
                                     avctx, frame, ctx);

    return 0;
}

static int nvtegra_hevc_end_frame(AVCodecContext *avctx) {
    HEVCContext                *s = avctx->priv_data;
    NVTegraHEVCDecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    AVFrame                *frame = s->cur_frame->f;
    FrameDecodeData          *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame      *tf = fdd->hwaccel_priv;

    nvdec_hevc_pic_s *setup;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Ending HEVC-NVTEGRA frame with %u slices -> %u bytes\n",
           ctx->core.num_slices, ctx->core.bitstream_len);

    if (!tf || !ctx->core.num_slices)
        return 0;

    mem = av_nvtegra_map_get_addr((AVNVTegraMap *)tf->input_map_ref->data);

    setup = (nvdec_hevc_pic_s *)(mem + ctx->core.pic_setup_off);
    setup->stream_len = ctx->core.bitstream_len;

    err = nvtegra_hevc_prepare_cmdbuf(&ctx->core.cmdbuf, s, ctx, frame);
    if (err < 0)
        return err;

    return ff_nvtegra_end_frame(avctx, frame, &ctx->core, NULL, 0);
}

static int nvtegra_hevc_decode_slice(AVCodecContext *avctx, const uint8_t *buf,
                                     uint32_t buf_size)
{
    HEVCContext                *s = avctx->priv_data;
    AVFrame                *frame = s->cur_frame->f;
    FrameDecodeData          *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame      *tf = fdd->hwaccel_priv;
    AVNVTegraMap       *input_map = (AVNVTegraMap *)tf->input_map_ref->data;
    NVTegraHEVCDecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    uint8_t *mem;

    mem = av_nvtegra_map_get_addr(input_map);

    /*
     * Official code adds a 4-byte 00000001 startcode,
     * though decoding was observed to work without it
     */
    AV_WB8(mem + ctx->core.bitstream_off + ctx->core.bitstream_len, 0);
    ctx->core.bitstream_len += 1;

    return ff_nvtegra_decode_slice(avctx, frame, buf, buf_size, AV_RB24(buf) != 1);
}

#if CONFIG_HEVC_NVTEGRA_HWACCEL
const FFHWAccel ff_hevc_nvtegra_hwaccel = {
    .p.name               = "hevc_nvtegra",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_HEVC,
    .p.pix_fmt            = AV_PIX_FMT_NVTEGRA,
    .start_frame          = &nvtegra_hevc_start_frame,
    .end_frame            = &nvtegra_hevc_end_frame,
    .decode_slice         = &nvtegra_hevc_decode_slice,
    .init                 = &nvtegra_hevc_decode_init,
    .uninit               = &nvtegra_hevc_decode_uninit,
    .frame_params         = &ff_nvtegra_frame_params,
    .frame_priv_data_size = sizeof(NVTegraHEVCFrameData),
    .priv_data_size       = sizeof(NVTegraHEVCDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
#endif
