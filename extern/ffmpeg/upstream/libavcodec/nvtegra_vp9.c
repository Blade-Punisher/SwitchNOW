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
#include "vp9data.h"
#include "vp9dec.h"
#include "decode.h"
#include "nvtegra_decode.h"

#include "libavutil/pixdesc.h"
#include "libavutil/nvtegra_host1x.h"

typedef struct NVTegraVP9DecodeContext {
    FFNVTegraDecodeContext core;

    uint32_t prob_tab_off;

    AVNVTegraMap common_map;
    uint32_t segment_rw1_off, segment_rw2_off, tile_sizes_off, filter_off,
             col_mvrw1_off, col_mvrw2_off, ctx_counter_off;

    bool prev_show_frame;

    AVFrame *refs[3];
} NVTegraVP9DecodeContext;

/* Size (width, height) of a macroblock */
#define MB_SIZE 16

/* Maximum size (width, height) of a superblock */
#define SB_SIZE 64

#define CEILDIV(a, b) (((a) + (b) - 1) / (b))

/* Prediction modes aren't layed out in the same order in ffmpeg's defaults than in hardware */
static const uint8_t pmconv[] = { 2, 0, 1, 3, 4, 5, 6, 8, 7, 9 };

static int nvtegra_vp9_decode_uninit(AVCodecContext *avctx) {
    NVTegraVP9DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    int err;

    av_log(avctx, AV_LOG_DEBUG, "Deinitializing NVTEGRA VP9 decoder\n");

    err = av_nvtegra_map_destroy(&ctx->common_map);
    if (err < 0)
        return err;

    err = ff_nvtegra_decode_uninit(avctx, &ctx->core);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_vp9_decode_init(AVCodecContext *avctx) {
    NVTegraVP9DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    AVHWDeviceContext      *hw_device_ctx;
    AVNVTegraDeviceContext *device_hwctx;
    uint32_t aligned_width, aligned_height, max_sb_size,
             segment_rw_size, filter_size, col_mvrw_size, ctx_counter_size,
             common_map_size;
    uint8_t *mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Initializing NVTEGRA VP9 decoder\n");

    ctx->core.pic_setup_off  = 0;
    ctx->core.status_off     = FFALIGN(ctx->core.pic_setup_off + sizeof(nvdec_vp9_pic_s),
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.cmdbuf_off     = FFALIGN(ctx->core.status_off    + sizeof(nvdec_status_s),
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->prob_tab_off        = FFALIGN(ctx->core.cmdbuf_off    + 2*AV_NVTEGRA_MAP_ALIGN,
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.bitstream_off  = FFALIGN(ctx->prob_tab_off       + sizeof(nvdec_vp9EntropyProbs_t),
                                       AV_NVTEGRA_MAP_ALIGN);
    ctx->core.input_map_size = FFALIGN(ctx->core.bitstream_off + ff_nvtegra_decode_pick_bitstream_buffer_size(avctx),
                                       0x1000);

    ctx->core.max_cmdbuf_size    = ctx->prob_tab_off        - ctx->core.cmdbuf_off;
    ctx->core.max_bitstream_size = ctx->core.input_map_size - ctx->core.bitstream_off;

    err = ff_nvtegra_decode_init(avctx, &ctx->core);
    if (err < 0)
        goto fail;

    hw_device_ctx = (AVHWDeviceContext *)ctx->core.hw_device_ref->data;
    device_hwctx  = hw_device_ctx->hwctx;

    aligned_width    = FFALIGN(avctx->coded_width,  MB_SIZE);
    aligned_height   = FFALIGN(avctx->coded_height, MB_SIZE);
    max_sb_size      = CEILDIV(aligned_width, 64) * CEILDIV(aligned_height, 64);
    segment_rw_size  = FFALIGN(max_sb_size * 32, 0x100);
    filter_size      = FFALIGN(avctx->height, 64) * 988;
    col_mvrw_size    = max_sb_size * 1024;
    ctx_counter_size = FFALIGN(sizeof(nvdec_vp9EntropyCounts_t), 0x100);

    ctx->segment_rw1_off = 0;
    ctx->segment_rw2_off = FFALIGN(ctx->segment_rw1_off + segment_rw_size,  AV_NVTEGRA_MAP_ALIGN);
    ctx->tile_sizes_off  = FFALIGN(ctx->segment_rw2_off + segment_rw_size,  AV_NVTEGRA_MAP_ALIGN);
    ctx->filter_off      = FFALIGN(ctx->tile_sizes_off  + 0x700,            AV_NVTEGRA_MAP_ALIGN);
    ctx->col_mvrw1_off   = FFALIGN(ctx->filter_off      + filter_size,      AV_NVTEGRA_MAP_ALIGN);
    ctx->col_mvrw2_off   = FFALIGN(ctx->col_mvrw1_off   + col_mvrw_size,    AV_NVTEGRA_MAP_ALIGN);
    ctx->ctx_counter_off = FFALIGN(ctx->col_mvrw2_off   + col_mvrw_size,    AV_NVTEGRA_MAP_ALIGN);
    common_map_size      = FFALIGN(ctx->ctx_counter_off + ctx_counter_size, 0x1000);

    err = av_nvtegra_map_create(&ctx->common_map, &device_hwctx->nvdec_channel, common_map_size, 0x100,
                                NVMAP_HEAP_IOVMM, NVMAP_HANDLE_WRITE_COMBINE);
    if (err < 0)
        goto fail;

    mem = av_nvtegra_map_get_addr(&ctx->common_map);

    memset(mem + ctx->segment_rw1_off, 0, segment_rw_size);
    memset(mem + ctx->segment_rw2_off, 0, segment_rw_size);

    memset(mem + ctx->tile_sizes_off, 0, 0x700);
    ((uint16_t *)(mem + ctx->tile_sizes_off))[0x37a] = 9;
    ((uint16_t *)(mem + ctx->tile_sizes_off))[0x37b] = 1;

    memset(mem + ctx->col_mvrw1_off, 0, col_mvrw_size);
    memset(mem + ctx->col_mvrw2_off, 0, col_mvrw_size);

    memset(mem + ctx->ctx_counter_off, 0, sizeof(nvdec_vp9EntropyCounts_t));

    return 0;

fail:
    nvtegra_vp9_decode_uninit(avctx);
    return err;
}

static void nvtegra_vp9_init_probs(nvdec_vp9EntropyProbs_t *probs) {
    int i, j;

    for (i = 0; i < FF_ARRAY_ELEMS(probs->kf_bmode_prob); ++i) {
        for (j = 0; j < FF_ARRAY_ELEMS(probs->kf_bmode_prob[0]); ++j) {
            memcpy(probs->kf_bmode_prob[i][j], ff_vp9_default_kf_ymode_probs[pmconv[i]][pmconv[j]], 8);
            probs->kf_bmode_probB[i][j][0]   = ff_vp9_default_kf_ymode_probs[pmconv[i]][pmconv[j]][8];
        }
        memcpy(probs->kf_uv_mode_prob[i], ff_vp9_default_kf_uvmode_probs[pmconv[i]], 8);
        probs->kf_uv_mode_probB[i][0]   = ff_vp9_default_kf_uvmode_probs[pmconv[i]][8];
    }
}

static void nvtegra_vp9_update_probs(nvdec_vp9EntropyProbs_t *probs,
                                     VP9Context *s, bool init)
{
    ProbContext *p = &s->prob.p;

    int i, j, k, l;

    if (init) {
        memset(probs, 0, sizeof(nvdec_vp9EntropyProbs_t));
        nvtegra_vp9_init_probs(probs);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(probs->ref_pred_probs); ++i)
        probs->ref_pred_probs[i] = *s->intra_pred_data[i];

    memcpy(probs->mb_segment_tree_probs,  s->s.h.segmentation.prob,      sizeof(probs->mb_segment_tree_probs));
    if (s->s.h.segmentation.temporal)
        memcpy(probs->segment_pred_probs, s->s.h.segmentation.pred_prob, sizeof(probs->segment_pred_probs));
    else
        memset(probs->segment_pred_probs, 0xff, sizeof(probs->segment_pred_probs));

    /* Ignored by official software: ref_scores, prob_comppred */

    for (i = 0; i < FF_ARRAY_ELEMS(probs->a.inter_mode_prob); ++i)
        memcpy(probs->a.inter_mode_prob[i], p->mv_mode[i], 3);

    memcpy(probs->a.intra_inter_prob, p->intra, sizeof(probs->a.intra_inter_prob));

    for (i = 0; i < FF_ARRAY_ELEMS(probs->a.uv_mode_prob); ++i) {
        memcpy(probs->a.uv_mode_prob[i], p->uv_mode[pmconv[i]], 8);
        probs->a.uv_mode_probB[i][0]   = p->uv_mode[pmconv[i]][8];
    }

    for (i = 0; i < FF_ARRAY_ELEMS(probs->a.tx8x8_prob); ++i) {
        memcpy(probs->a.tx8x8_prob  [i], &p->tx8p [i], 1);
        memcpy(probs->a.tx16x16_prob[i],  p->tx16p[i], 2);
        memcpy(probs->a.tx32x32_prob[i],  p->tx32p[i], 3);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(probs->a.sb_ymode_prob); ++i) {
        memcpy(probs->a.sb_ymode_prob[i], p->y_mode[i], 8);
        probs->a.sb_ymode_probB[i][0]   = p->y_mode[i][8];
    }

    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            memcpy(probs->a.partition_prob[0][4*(3-i)+j],
                   &ff_vp9_default_kf_partition_probs[i][j], 3);
            memcpy(probs->a.partition_prob[1][4*(3-i)+j], &p->partition[i][j], 3);
        }
    }

    memcpy(probs->a.switchable_interp_prob, p->filter, sizeof(probs->a.switchable_interp_prob));
    memcpy(probs->a.comp_inter_prob,        p->comp,   sizeof(probs->a.comp_inter_prob));
    memcpy(probs->a.mbskip_probs,           p->skip,   sizeof(probs->a.mbskip_probs));

    memcpy(probs->a.nmvc.joints, p->mv_joint, 3);
    for (i = 0; i < FF_ARRAY_ELEMS(p->mv_comp); ++i) {
        probs->a.nmvc.sign     [i]       = p->mv_comp[i].sign;
        probs->a.nmvc.class0   [i][0]    = p->mv_comp[i].class0;
        probs->a.nmvc.class0_hp[i]       = p->mv_comp[i].class0_hp;
        probs->a.nmvc.hp       [i]       = p->mv_comp[i].hp;
        memcpy(probs->a.nmvc.fp       [i], p->mv_comp[i].fp,        3);
        memcpy(probs->a.nmvc.classes  [i], p->mv_comp[i].classes,   10);
        memcpy(probs->a.nmvc.class0_fp[i], p->mv_comp[i].class0_fp, 2 * 3);
        memcpy(probs->a.nmvc.bits     [i], p->mv_comp[i].bits,      10);
    }

    memcpy(probs->a.single_ref_prob, p->single_ref, sizeof(probs->a.single_ref_prob));
    memcpy(probs->a.comp_ref_prob,   p->comp_ref,   sizeof(probs->a.comp_ref_prob));

    for (i = 0; i < FF_ARRAY_ELEMS(probs->a.probCoeffs); ++i) {
        for (j = 0; j < FF_ARRAY_ELEMS(probs->a.probCoeffs[0]); ++j) {
            for (k = 0; k < FF_ARRAY_ELEMS(probs->a.probCoeffs[0][0]); ++k) {
                for (l = 0; l < FF_ARRAY_ELEMS(probs->a.probCoeffs[0][0][0]); ++l) {
                    memcpy(probs->a.probCoeffs     [i][j][k][l], s->prob.coef[0][i][j][k][l], 3);
                    memcpy(probs->a.probCoeffs8x8  [i][j][k][l], s->prob.coef[1][i][j][k][l], 3);
                    memcpy(probs->a.probCoeffs16x16[i][j][k][l], s->prob.coef[2][i][j][k][l], 3);
                    memcpy(probs->a.probCoeffs32x32[i][j][k][l], s->prob.coef[3][i][j][k][l], 3);
                }
            }
        }
    }
}

static void nvtegra_vp9_set_tile_sizes(uint16_t *sizes, VP9Context *s) {
    int i, j;

    for (i = 0; i < s->s.h.tiling.tile_rows; ++i) {
        for (j = 0; j < s->s.h.tiling.tile_cols; ++j) {
            sizes[0] = (s->sb_cols * (j + 1) >> s->s.h.tiling.log2_tile_cols) -
                       (s->sb_cols *  j      >> s->s.h.tiling.log2_tile_cols);
            sizes[1] = (s->sb_rows * (i + 1) >> s->s.h.tiling.log2_tile_rows) -
                       (s->sb_rows *  i      >> s->s.h.tiling.log2_tile_rows);
            sizes += 2;
        }
    }
}

static void nvtegra_vp9_update_counts(nvdec_vp9EntropyCounts_t *cts,
                                      VP9TileData *td)
{
    int i, j, k, l;

    for (i = 0; i < FF_ARRAY_ELEMS(td->counts.y_mode); ++i) {
        for (j = 0; j < FF_ARRAY_ELEMS(td->counts.y_mode[0]); ++j) {
            td->counts.y_mode[i][pmconv[j]] = cts->sb_ymode_counts[i][j];
        }
    }

    for (i = 0; i < FF_ARRAY_ELEMS(td->counts.uv_mode); ++i) {
        for (j = 0; j < FF_ARRAY_ELEMS(td->counts.uv_mode[0]); ++j) {
            td->counts.uv_mode[pmconv[i]][pmconv[j]] = cts->uv_mode_counts[i][j];
        }
    }

    memcpy(td->counts.filter,     cts->switchable_interp_counts, sizeof(td->counts.filter));
    memcpy(td->counts.intra,      cts->intra_inter_count,        sizeof(td->counts.intra));
    memcpy(td->counts.comp,       cts->comp_inter_count,         sizeof(td->counts.comp));
    memcpy(td->counts.single_ref, cts->single_ref_count,         sizeof(td->counts.single_ref));
    memcpy(td->counts.tx32p,      cts->tx32x32_count,            sizeof(td->counts.tx32p));
    memcpy(td->counts.tx16p,      cts->tx16x16_count,            sizeof(td->counts.tx16p));
    memcpy(td->counts.tx8p,       cts->tx8x8_count,              sizeof(td->counts.tx8p));
    memcpy(td->counts.skip,       cts->mbskip_count,             sizeof(td->counts.skip));

    for (i = 0; i < FF_ARRAY_ELEMS(td->counts.mv_mode); ++i) {
        td->counts.mv_mode[i][0] = cts->inter_mode_counts[i][1][0];
        td->counts.mv_mode[i][1] = cts->inter_mode_counts[i][2][0];
        td->counts.mv_mode[i][2] = cts->inter_mode_counts[i][0][0];
        td->counts.mv_mode[i][3] = cts->inter_mode_counts[i][2][1];
    }

    memcpy(td->counts.mv_joint,                 cts->nmvcount.joints,       sizeof(td->counts.mv_joint));
    for (i = 0; i < FF_ARRAY_ELEMS(td->counts.mv_comp); ++i) {
        memcpy(td->counts.mv_comp[i].sign,      cts->nmvcount.sign     [i], sizeof(td->counts.mv_comp[i].sign));
        memcpy(td->counts.mv_comp[i].classes,   cts->nmvcount.classes  [i], sizeof(td->counts.mv_comp[i].classes));
        memcpy(td->counts.mv_comp[i].class0,    cts->nmvcount.class0   [i], sizeof(td->counts.mv_comp[i].class0));
        memcpy(td->counts.mv_comp[i].bits,      cts->nmvcount.bits     [i], sizeof(td->counts.mv_comp[i].bits));
        memcpy(td->counts.mv_comp[i].class0_fp, cts->nmvcount.class0_fp[i], sizeof(td->counts.mv_comp[i].class0_fp));
        memcpy(td->counts.mv_comp[i].fp,        cts->nmvcount.fp       [i], sizeof(td->counts.mv_comp[i].fp));
        memcpy(td->counts.mv_comp[i].class0_hp, cts->nmvcount.class0_hp[i], sizeof(td->counts.mv_comp[i].class0_hp));
        memcpy(td->counts.mv_comp[i].hp,        cts->nmvcount.hp       [i], sizeof(td->counts.mv_comp[i].hp));
    }

    memcpy(td->counts.partition[0], cts->partition_counts[12], sizeof(td->counts.partition[0]));
    memcpy(td->counts.partition[1], cts->partition_counts[ 8], sizeof(td->counts.partition[1]));
    memcpy(td->counts.partition[2], cts->partition_counts[ 4], sizeof(td->counts.partition[2]));
    memcpy(td->counts.partition[3], cts->partition_counts[ 0], sizeof(td->counts.partition[3]));

    for (i = 0; i < FF_ARRAY_ELEMS(td->counts.coef[0]); ++i) {
        for (j = 0; j < FF_ARRAY_ELEMS(td->counts.coef[0][0]); ++j) {
            for (k = 0; k < FF_ARRAY_ELEMS(td->counts.coef[0][0][0]); ++k) {
                for (l = 0; l < FF_ARRAY_ELEMS(td->counts.coef[0][0][0][0]); ++l) {
                    memcpy(td->counts.coef[0][i][j][k][l], cts->countCoeffs     [i][j][k][l],
                        sizeof(td->counts.coef[0][i][j][k][l]));
                    memcpy(td->counts.coef[1][i][j][k][l], cts->countCoeffs8x8  [i][j][k][l],
                        sizeof(td->counts.coef[1][i][j][k][l]));
                    memcpy(td->counts.coef[2][i][j][k][l], cts->countCoeffs16x16[i][j][k][l],
                        sizeof(td->counts.coef[2][i][j][k][l]));
                    memcpy(td->counts.coef[3][i][j][k][l], cts->countCoeffs32x32[i][j][k][l],
                        sizeof(td->counts.coef[3][i][j][k][l]));
                    td->counts.eob[0][i][j][k][l][0] = cts->countCoeffs     [i][j][k][l][3];
                    td->counts.eob[0][i][j][k][l][1] = cts->countEobs[0][i][j][k][l] - td->counts.eob[0][i][j][k][l][0];
                    td->counts.eob[1][i][j][k][l][0] = cts->countCoeffs8x8  [i][j][k][l][3];
                    td->counts.eob[1][i][j][k][l][1] = cts->countEobs[1][i][j][k][l] - td->counts.eob[1][i][j][k][l][0];
                    td->counts.eob[2][i][j][k][l][0] = cts->countCoeffs16x16[i][j][k][l][3];
                    td->counts.eob[2][i][j][k][l][1] = cts->countEobs[2][i][j][k][l] - td->counts.eob[2][i][j][k][l][0];
                    td->counts.eob[3][i][j][k][l][0] = cts->countCoeffs32x32[i][j][k][l][3];
                    td->counts.eob[3][i][j][k][l][1] = cts->countEobs[3][i][j][k][l] - td->counts.eob[3][i][j][k][l][0];
                }
            }
        }
    }
}

static void nvtegra_vp9_prepare_frame_setup(nvdec_vp9_pic_s *setup, AVCodecContext *avctx,
                                            NVTegraVP9DecodeContext *ctx)
{
    VP9Context       *s = avctx->priv_data;
    VP9SharedContext *h = &s->s;

    int i;

    /* Note: the stride is divided by 2 when the depth is > 8 (not supported on T210) */
#define FWIDTH(f)      ((f && f->private_ref) ? f->width       : 0)
#define FHEIGHT(f)     ((f && f->private_ref) ? f->height      : 0)
#define FSTRIDE(f, c)  ((f && f->private_ref) ? f->linesize[c] : 0)

    /* Note: the v1 substructure isn't filled out on T210 */
    *setup = (nvdec_vp9_pic_s){
        .gptimer_timeout_value    = 0, /* Default value */

        .tileformat               = 0, /* TBL */
        .gob_height               = 0, /* GOB_2 */

        .Vp9BsdCtrlOffset         = FFALIGN(avctx->height, 64) * 912 / 256,

        .ref0_width               = FWIDTH (h->refs[h->h.refidx[0]].f),
        .ref0_height              = FHEIGHT(h->refs[h->h.refidx[0]].f),
        .ref0_stride              = {
            FSTRIDE(h->refs[h->h.refidx[0]].f, 0),
            FSTRIDE(h->refs[h->h.refidx[0]].f, 1),
        },

        .ref1_width               = FWIDTH (h->refs[h->h.refidx[1]].f),
        .ref1_height              = FHEIGHT(h->refs[h->h.refidx[1]].f),
        .ref1_stride              = {
            FSTRIDE(h->refs[h->h.refidx[1]].f, 0),
            FSTRIDE(h->refs[h->h.refidx[1]].f, 1),
        },

        .ref2_width               = FWIDTH (h->refs[h->h.refidx[2]].f),
        .ref2_height              = FHEIGHT(h->refs[h->h.refidx[2]].f),
        .ref2_stride              = {
            FSTRIDE(h->refs[h->h.refidx[2]].f, 0),
            FSTRIDE(h->refs[h->h.refidx[2]].f, 1),
        },

        .width                    = FWIDTH (h->frames[CUR_FRAME].tf.f),
        .height                   = FHEIGHT(h->frames[CUR_FRAME].tf.f),
        .framestride              = {
            FSTRIDE(h->frames[CUR_FRAME].tf.f, 0),
            FSTRIDE(h->frames[CUR_FRAME].tf.f, 1),
        },

        .keyFrame                 = h->h.keyframe,
        .prevIsKeyFrame           = s->last_keyframe,
        .errorResilient           = h->h.errorres,
        .prevShowFrame            = ctx->prev_show_frame,
        .intraOnly                = h->h.intraonly,

        .refFrameSignBias         = {
            0,
            h->h.signbias[0], h->h.signbias[1], h->h.signbias[2],
        },

        .loopFilterLevel          = h->h.filter.level,
        .loopFilterSharpness      = h->h.filter.sharpness,

        .qpYAc                    = h->h.yac_qi,
        .qpYDc                    = h->h.ydc_qdelta,
        .qpChAc                   = h->h.uvdc_qdelta,
        .qpChDc                   = h->h.uvac_qdelta,

        .lossless                 = h->h.lossless,
        .transform_mode           = h->h.txfmmode,
        .allow_high_precision_mv  = h->h.keyframe ? 0 : h->h.highprecisionmvs,
        .mcomp_filter_type        = h->h.filtermode,
        .comp_pred_mode           = h->h.comppredmode,
        .comp_fixed_ref           = h->h.allowcompinter ? h->h.fixcompref + 1 : 0,
        .comp_var_ref             = {
            h->h.allowcompinter ? h->h.varcompref[0] + 1 : 0,
            h->h.allowcompinter ? h->h.varcompref[1] + 1 : 0,
        },

        .log2_tile_columns        = h->h.tiling.log2_tile_cols,
        .log2_tile_rows           = h->h.tiling.log2_tile_rows,

        .segmentEnabled           = h->h.segmentation.enabled,
        .segmentMapUpdate         = h->h.segmentation.update_map,
        .segmentMapTemporalUpdate = h->h.segmentation.temporal,
        .segmentFeatureMode       = h->h.segmentation.absolute_vals,
        .modeRefLfEnabled         = h->h.lf_delta.enabled,
        .mbRefLfDelta             = {
            h->h.lf_delta.ref[0],  h->h.lf_delta.ref[1],
            h->h.lf_delta.ref[2],  h->h.lf_delta.ref[3],
        },
        .mbModeLfDelta            = {
            h->h.lf_delta.mode[0], h->h.lf_delta.mode[1],
        },
    };

    for (i = 0; i < 8; ++i) {
        setup->segmentFeatureEnable[i][0] = h->h.segmentation.feat[i].q_enabled;
        setup->segmentFeatureEnable[i][1] = h->h.segmentation.feat[i].lf_enabled;
        setup->segmentFeatureEnable[i][2] = h->h.segmentation.feat[i].ref_enabled;
        setup->segmentFeatureEnable[i][3] = h->h.segmentation.feat[i].skip_enabled;

        setup->segmentFeatureData[i][0]   = h->h.segmentation.feat[i].q_val;
        setup->segmentFeatureData[i][1]   = h->h.segmentation.feat[i].lf_val;
        setup->segmentFeatureData[i][2]   = h->h.segmentation.feat[i].ref_val;
        setup->segmentFeatureData[i][3]   = 0;
    }

    ctx->prev_show_frame = !h->h.invisible;
}

static int nvtegra_vp9_prepare_cmdbuf(AVNVTegraCmdbuf *cmdbuf, VP9SharedContext *h,
                                      NVTegraVP9DecodeContext *ctx, AVFrame *cur_frame)
{
    FrameDecodeData     *fdd = (FrameDecodeData *)cur_frame->private_ref->data;
    FFNVTegraDecodeFrame *tf = fdd->hwaccel_priv;
    AVNVTegraMap  *input_map = (AVNVTegraMap *)tf->input_map_ref->data;

    uint32_t col_mvwrite_off, col_mvread_off;
    int err;

    if (ctx->core.frame_idx % 2 == 0)
        col_mvwrite_off = ctx->col_mvrw1_off, col_mvread_off = ctx->col_mvrw2_off;
    else
        col_mvwrite_off = ctx->col_mvrw2_off, col_mvread_off = ctx->col_mvrw1_off;

    err = av_nvtegra_cmdbuf_begin(cmdbuf, HOST1X_CLASS_NVDEC);
    if (err < 0)
        return err;

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_APPLICATION_ID,
                          AV_NVTEGRA_ENUM(NVC5B0_SET_APPLICATION_ID, ID, VP9));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_CONTROL_PARAMS,
                          AV_NVTEGRA_ENUM (NVC5B0_SET_CONTROL_PARAMS, CODEC_TYPE,     VP9) |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, ERR_CONCEAL_ON, 1)   |
                          AV_NVTEGRA_VALUE(NVC5B0_SET_CONTROL_PARAMS, GPTIMER_ON,     1));
    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_SET_PICTURE_INDEX,
                          AV_NVTEGRA_VALUE(NVC5B0_SET_PICTURE_INDEX, INDEX, ctx->core.frame_idx));

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_DRV_PIC_SETUP_OFFSET,
                          input_map,        ctx->core.pic_setup_off,     NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_IN_BUF_BASE_OFFSET,
                          input_map,        ctx->core.bitstream_off,     NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_NVDEC_STATUS_OFFSET,
                          input_map,        ctx->core.status_off,        NVHOST_RELOC_TYPE_DEFAULT);

    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_VP9_SET_PROB_TAB_BUF_OFFSET,
                          input_map,        ctx->prob_tab_off,           NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_VP9_SET_CTX_COUNTER_BUF_OFFSET,
                          &ctx->common_map, ctx->ctx_counter_off,        NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_VP9_SET_TILE_SIZE_BUF_OFFSET,
                          &ctx->common_map, ctx->tile_sizes_off,         NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_VP9_SET_COL_MVWRITE_BUF_OFFSET,
                          &ctx->common_map, col_mvwrite_off,             NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_VP9_SET_COL_MVREAD_BUF_OFFSET,
                          &ctx->common_map, col_mvread_off,              NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_VP9_SET_SEGMENT_READ_BUF_OFFSET,
                          &ctx->common_map, ctx->segment_rw1_off,        NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_VP9_SET_SEGMENT_WRITE_BUF_OFFSET,
                          &ctx->common_map, ctx->segment_rw2_off,        NVHOST_RELOC_TYPE_DEFAULT);
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_VP9_SET_FILTER_BUFFER_OFFSET,
                          &ctx->common_map, ctx->filter_off,             NVHOST_RELOC_TYPE_DEFAULT);

#define PUSH_FRAME(fr, offset) ({                                                           \
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PICTURE_LUMA_OFFSET0   + offset * 4,           \
                          av_nvtegra_frame_get_fbuf_map(fr), 0, NVHOST_RELOC_TYPE_DEFAULT); \
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVC5B0_SET_PICTURE_CHROMA_OFFSET0 + offset * 4,           \
                          av_nvtegra_frame_get_fbuf_map(fr), fr->data[1] - fr->data[0],     \
                          NVHOST_RELOC_TYPE_DEFAULT);                                       \
})

    PUSH_FRAME(ctx->refs[0], 0);
    PUSH_FRAME(ctx->refs[1], 1);
    PUSH_FRAME(ctx->refs[2], 2);
    PUSH_FRAME(cur_frame,    3);

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVC5B0_EXECUTE,
                          AV_NVTEGRA_ENUM(NVC5B0_EXECUTE, AWAKEN, ENABLE));

    err = av_nvtegra_cmdbuf_end(cmdbuf);
    if (err < 0)
        return err;

    if (h->h.segmentation.update_map)
        FFSWAP(uint32_t, ctx->segment_rw1_off, ctx->segment_rw2_off);

    return 0;
}

static int nvtegra_vp9_start_frame(AVCodecContext *avctx, const uint8_t *buf, uint32_t buf_size) {
    VP9Context                *s = avctx->priv_data;
    VP9SharedContext          *h = &s->s;
    AVFrame               *frame = h->frames[CUR_FRAME].tf.f;
    FrameDecodeData         *fdd = (FrameDecodeData *)frame->private_ref->data;
    NVTegraVP9DecodeContext *ctx = avctx->internal->hwaccel_priv_data;

    FFNVTegraDecodeFrame *tf;
    AVNVTegraMap *input_map;
    uint8_t *mem, *common_mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Starting VP9-NVTEGRA frame with pixel format %s\n",
           av_get_pix_fmt_name(avctx->sw_pix_fmt));

    if (s->s.h.refreshctx && s->s.h.parallelmode) {
        int i, j, k, l, m;

        for (i = 0; i < FF_ARRAY_ELEMS(s->prob_ctx[s->s.h.framectxid].coef); i++) {
            for (j = 0; j < FF_ARRAY_ELEMS(s->prob_ctx[s->s.h.framectxid].coef[0]); j++)
                for (k = 0; k < FF_ARRAY_ELEMS(s->prob_ctx[s->s.h.framectxid].coef[0][0]); k++)
                    for (l = 0; l < FF_ARRAY_ELEMS(s->prob_ctx[s->s.h.framectxid].coef[0][0][0]); l++)
                        for (m = 0; m < FF_ARRAY_ELEMS(s->prob_ctx[s->s.h.framectxid].coef[0][0][0][0]); m++)
                            memcpy(s->prob_ctx[s->s.h.framectxid].coef[i][j][k][l][m],
                                   s->prob.coef[i][j][k][l][m],
                                   FF_ARRAY_ELEMS(s->prob_ctx[s->s.h.framectxid].coef[0][0][0][0][0]));
            if (s->s.h.txfmmode == i)
                break;
        }

        s->prob_ctx[s->s.h.framectxid].p = s->prob.p;
    }

    err = ff_nvtegra_start_frame(avctx, frame, &ctx->core);
    if (err < 0)
        return err;

    tf = fdd->hwaccel_priv;
    input_map = (AVNVTegraMap *)tf->input_map_ref->data;
    mem = av_nvtegra_map_get_addr(input_map), common_mem = av_nvtegra_map_get_addr(&ctx->common_map);

    nvtegra_vp9_prepare_frame_setup((nvdec_vp9_pic_s *)(mem + ctx->core.pic_setup_off), avctx, ctx);
    nvtegra_vp9_set_tile_sizes((uint16_t *)(common_mem + ctx->tile_sizes_off), s);
    nvtegra_vp9_update_probs((nvdec_vp9EntropyProbs_t *)(mem + ctx->prob_tab_off), s, ctx->core.new_input_buffer);

    ctx->refs[0] = ff_nvtegra_safe_get_ref(h->refs[h->h.refidx[0]].f, h->frames[CUR_FRAME].tf.f);
    ctx->refs[1] = ff_nvtegra_safe_get_ref(h->refs[h->h.refidx[1]].f, h->frames[CUR_FRAME].tf.f);
    ctx->refs[2] = ff_nvtegra_safe_get_ref(h->refs[h->h.refidx[2]].f, h->frames[CUR_FRAME].tf.f);

    return 0;
}

static int nvtegra_vp9_end_frame(AVCodecContext *avctx) {
    VP9Context                *s = avctx->priv_data;
    VP9SharedContext          *h = avctx->priv_data;
    NVTegraVP9DecodeContext *ctx = avctx->internal->hwaccel_priv_data;
    AVFrame               *frame = h->frames[CUR_FRAME].tf.f;
    FrameDecodeData         *fdd = (FrameDecodeData *)frame->private_ref->data;
    FFNVTegraDecodeFrame     *tf = fdd->hwaccel_priv;

    nvdec_vp9_pic_s *setup;
    uint8_t *mem, *common_mem;
    int err;

    av_log(avctx, AV_LOG_DEBUG, "Ending VP9-NVTEGRA frame with %u slices -> %u bytes\n",
           ctx->core.num_slices, ctx->core.bitstream_len);

    if (!tf || !ctx->core.num_slices)
        return 0;

    mem = av_nvtegra_map_get_addr((AVNVTegraMap *)tf->input_map_ref->data);

    setup = (nvdec_vp9_pic_s *)(mem + ctx->core.pic_setup_off);
    setup->stream_len = ctx->core.bitstream_len;

    err = nvtegra_vp9_prepare_cmdbuf(&ctx->core.cmdbuf, h, ctx, frame);
    if (err < 0)
        return err;

    err = ff_nvtegra_end_frame(avctx, frame, &ctx->core, NULL, 0);
    if (err < 0)
        return err;

    /*
     * Perform backward probability updates if necessary.
     * Since it depends on entropy counts calculated by the hardware,
     * we need to wait for the decode operation to complete.
     */
    if (!s->s.h.errorres && !s->s.h.parallelmode) {
        err = ff_nvtegra_wait_decode(avctx, frame);
        if (err < 0)
            return err;

        common_mem = av_nvtegra_map_get_addr(&ctx->common_map);

        nvtegra_vp9_update_counts((nvdec_vp9EntropyCounts_t *)(common_mem + ctx->ctx_counter_off),
                                  s->td);
        ff_vp9_adapt_probs(s);
    }

    return 0;
}

static int nvtegra_vp9_decode_slice(AVCodecContext *avctx, const uint8_t *buf,
                                    uint32_t buf_size)
{
    VP9SharedContext *h = avctx->priv_data;
    AVFrame      *frame = h->frames[CUR_FRAME].tf.f;

    int offset = h->h.uncompressed_header_size + h->h.compressed_header_size;

    return ff_nvtegra_decode_slice(avctx, frame, buf + offset, buf_size - offset, false);
}

#if CONFIG_VP9_NVTEGRA_HWACCEL
const FFHWAccel ff_vp9_nvtegra_hwaccel = {
    .p.name         = "vp9_nvtegra",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VP9,
    .p.pix_fmt      = AV_PIX_FMT_NVTEGRA,
    .start_frame    = &nvtegra_vp9_start_frame,
    .end_frame      = &nvtegra_vp9_end_frame,
    .decode_slice   = &nvtegra_vp9_decode_slice,
    .init           = &nvtegra_vp9_decode_init,
    .uninit         = &nvtegra_vp9_decode_uninit,
    .frame_params   = &ff_nvtegra_frame_params,
    .priv_data_size = sizeof(NVTegraVP9DecodeContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif
