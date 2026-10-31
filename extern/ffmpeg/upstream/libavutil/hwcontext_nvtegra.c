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

#include <stdbool.h>

#include "config.h"
#include "pixdesc.h"
#include "imgutils.h"
#include "internal.h"
#include "mem.h"
#include "time.h"

#include "hwcontext.h"
#include "hwcontext_internal.h"

#include "nvhost_ioctl.h"
#include "nvmap_ioctl.h"
#include "nvtegra_host1x.h"
#include "clb0b6.h"
#include "vic_drv.h"

#include "hwcontext_nvtegra.h"

typedef struct NVTegraDevicePriv {
    /* The public AVNVTegraDeviceContext */
    AVNVTegraDeviceContext p;

    AVBufferRef *driver_state_ref;

    AVNVTegraJobPool job_pool;
    uint32_t vic_setup_off, vic_cmdbuf_off;

    double framerate;
    uint32_t dfs_lowcorner;
    double dfs_decode_cycles_ema;
    double dfs_ema_damping;
    int dfs_bitrate_sum;
    int dfs_cur_sample, dfs_num_samples;
    int64_t dfs_sampling_start_ts, dfs_last_ts_delta;
} NVTegraDevicePriv;

static const enum AVPixelFormat supported_sw_formats[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_YUV420P,
};

int av_nvtegra_pixfmt_to_vic(enum AVPixelFormat fmt) {
    switch (fmt) {
        case AV_PIX_FMT_GRAY8:
            return NVB0B6_T_L8;
        case AV_PIX_FMT_NV12:
            return NVB0B6_T_Y8___U8V8_N420;
        case AV_PIX_FMT_YUV420P:
            return NVB0B6_T_Y8___U8___V8_N420;
        case AV_PIX_FMT_RGB565:
            return NVB0B6_T_R5G6B5;
        case AV_PIX_FMT_RGB32:
            return NVB0B6_T_A8R8G8B8;
        case AV_PIX_FMT_BGR32:
            return NVB0B6_T_A8B8G8R8;
        case AV_PIX_FMT_RGB32_1:
            return NVB0B6_T_R8G8B8A8;
        case AV_PIX_FMT_BGR32_1:
            return NVB0B6_T_B8G8R8A8;
        case AV_PIX_FMT_0RGB32:
            return NVB0B6_T_X8R8G8B8;
        case AV_PIX_FMT_0BGR32:
            return NVB0B6_T_X8B8G8R8;
        default:
            return -1;
    }
}

static inline uint32_t nvtegra_surface_get_width_align(enum AVPixelFormat fmt, const AVComponentDescriptor *comp) {
    int step = comp->step;

    if (fmt != AV_PIX_FMT_NVTEGRA)
        return 256 / step; /* Pitch linear surfaces must be aligned to 256B for VIC */

    /*
     * GOBs are 64B wide.
     * In addition, we use a 32Bx8 cache width in VIC for block linear surfaces.
     */
    return 64 / step;
}

static inline uint32_t nvtegra_surface_get_height_align(enum AVPixelFormat fmt, const AVComponentDescriptor *comp) {
    /* Height alignment is in terms of lines, not bytes, therefore we don't divide by the sample step */
    if (fmt != AV_PIX_FMT_NVTEGRA)
        return 4; /* We use 64Bx4 cache width in VIC for pitch linear surfaces */

    /*
     * GOBs are 8B high, and we use a GOB height of 2.
     * In addition, we use a 32Bx8 cache width in VIC for block linear surfaces.
     * We double this requirement to make sure it is respected for the subsampled chroma plane.
     */
    return 32;
}

static int nvtegra_channel_set_freq(AVNVTegraChannel *channel, uint32_t freq) {
    int err;
#ifndef __SWITCH__
    err = av_nvtegra_channel_set_clock_rate(channel, channel->module_id, freq);
    if (err < 0)
        return err;

    err = av_nvtegra_channel_get_clock_rate(channel, channel->module_id, &channel->clock);
    if (err < 0)
        return err;
#else
    err = AVERROR(mmuRequestSetAndWait(&channel->mmu_request, freq, -1));
    if (err < 0)
        return err;

    err = AVERROR(mmuRequestGet(&channel->mmu_request, &channel->clock));
    if (err < 0)
        return err;
#endif
    return 0;
}

static void nvtegra_device_uninit(AVHWDeviceContext *ctx) {
    NVTegraDevicePriv       *priv = ctx->hwctx;
    AVNVTegraDeviceContext *hwctx = &priv->p;

    av_log(ctx, AV_LOG_DEBUG, "Deinitializing NVTEGRA device\n");

    av_nvtegra_job_pool_uninit(&priv->job_pool);

    if (hwctx->nvdec_version) {
        av_nvtegra_channel_close(&hwctx->nvdec_channel);
#ifdef __SWITCH__
        mmuRequestFinalize(&hwctx->nvdec_channel.mmu_request);
#endif
    }

    if (hwctx->nvjpg_version) {
        av_nvtegra_channel_close(&hwctx->nvjpg_channel);
#ifdef __SWITCH__
        mmuRequestFinalize(&hwctx->nvjpg_channel.mmu_request);
#endif
    }

    av_nvtegra_channel_close(&hwctx->vic_channel);

    av_buffer_unref(&priv->driver_state_ref);
}

/*
 * Hardware modules on the Tegra X1 (see t210.c in l4t kernel sources)
 * - nvdec v2.0
 * - nvenc v5.0
 * - nvjpg v1.0
 * - vic   v4.0
 */

static int nvtegra_device_init(AVHWDeviceContext *ctx) {
    NVTegraDevicePriv       *priv = ctx->hwctx;
    AVNVTegraDeviceContext *hwctx = &priv->p;

    uint32_t vic_map_size;
    int err;

    av_log(ctx, AV_LOG_DEBUG, "Initializing NVTEGRA device\n");

    err = av_nvtegra_channel_open(&hwctx->nvdec_channel, "/dev/nvhost-nvdec");
    if (!err)
        hwctx->nvdec_version = AV_NVTEGRA_ENCODE_REV(2,0);

    err = av_nvtegra_channel_open(&hwctx->nvjpg_channel, "/dev/nvhost-nvjpg");
    if (!err)
        hwctx->nvjpg_version = AV_NVTEGRA_ENCODE_REV(1,0);

    err = av_nvtegra_channel_open(&hwctx->vic_channel, "/dev/nvhost-vic");
    if (err < 0)
        goto fail;

    hwctx->vic_version = AV_NVTEGRA_ENCODE_REV(4,0);

    /* Note: Official code only sets this for the nvdec channel */
    if (hwctx->nvdec_version) {
        err = av_nvtegra_channel_set_submit_timeout(&hwctx->nvdec_channel, 1000);
        if (err < 0)
            goto fail;
    }

    if (hwctx->nvjpg_version) {
        err = av_nvtegra_channel_set_submit_timeout(&hwctx->nvjpg_channel, 1000);
        if (err < 0)
            goto fail;
    }

    priv->vic_setup_off  = 0;
    priv->vic_cmdbuf_off = FFALIGN(priv->vic_setup_off  + sizeof(VicConfigStruct),
                                   AV_NVTEGRA_MAP_ALIGN);
    vic_map_size         = FFALIGN(priv->vic_cmdbuf_off + AV_NVTEGRA_MAP_ALIGN,
                                   0x1000);

    err = av_nvtegra_job_pool_init(&priv->job_pool, &hwctx->vic_channel, vic_map_size,
                                   priv->vic_cmdbuf_off, vic_map_size - priv->vic_cmdbuf_off);
    if (err < 0)
        goto fail;

#ifndef __SWITCH__
    hwctx->nvdec_channel.module_id = 0x75;
    hwctx->nvjpg_channel.module_id = 0x76;
#else
    /*
     * The NVHOST_IOCTL_CHANNEL_SET_CLK_RATE ioctl also exists on HOS but the clock rate
     * will be reset when the console goes to sleep.
     */
    if (hwctx->nvdec_version) {
        err = AVERROR(mmuRequestInitialize(&hwctx->nvdec_channel.mmu_request, (MmuModuleId)5, 8, false));
        if (err < 0)
            goto fail;
    }

    if (hwctx->nvjpg_version) {
        err = AVERROR(mmuRequestInitialize(&hwctx->nvjpg_channel.mmu_request, MmuModuleId_Nvjpg, 8, false));
        if (err < 0)
            goto fail;
    }
#endif

    return 0;

fail:
    nvtegra_device_uninit(ctx);
    return err;
}

static int nvtegra_device_create(AVHWDeviceContext *ctx, const char *device,
                                 AVDictionary *opts, int flags)
{
    NVTegraDevicePriv *priv = ctx->hwctx;

    av_log(ctx, AV_LOG_DEBUG, "Creating NVTEGRA device\n");

    priv->driver_state_ref = av_nvtegra_driver_init();
    if (!priv->driver_state_ref) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create driver context, "
                                  "make sure you are using a Tegra device\n");
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int nvtegra_frames_get_constraints(AVHWDeviceContext *ctx, const void *hwconfig,
                                          AVHWFramesConstraints *constraints)
{
    av_log(ctx, AV_LOG_DEBUG, "Getting frame constraints for NVTEGRA device\n");

    constraints->valid_sw_formats = av_malloc_array(FF_ARRAY_ELEMS(supported_sw_formats) + 1,
                                                    sizeof(*constraints->valid_sw_formats));
    if (!constraints->valid_sw_formats)
        return AVERROR(ENOMEM);

    for (int i = 0; i < FF_ARRAY_ELEMS(supported_sw_formats); ++i)
        constraints->valid_sw_formats[i] = supported_sw_formats[i];
    constraints->valid_sw_formats[FF_ARRAY_ELEMS(supported_sw_formats)] = AV_PIX_FMT_NONE;

    constraints->valid_hw_formats = av_malloc_array(2, sizeof(*constraints->valid_hw_formats));
    if (!constraints->valid_hw_formats)
        return AVERROR(ENOMEM);

    constraints->valid_hw_formats[0] = AV_PIX_FMT_NVTEGRA;
    constraints->valid_hw_formats[1] = AV_PIX_FMT_NONE;

    return 0;
}

static void nvtegra_map_free(void *opaque, uint8_t *data) {
    AVNVTegraMap *map = (AVNVTegraMap *)data;

    if (!map)
        return;

    av_nvtegra_map_destroy(map);

    av_freep(&map);
}

static void nvtegra_frame_free(void *opaque, uint8_t *data) {
    AVNVTegraFrame *frame = (AVNVTegraFrame *)data;

    if (!frame)
        return;

    av_buffer_unref(&frame->map_ref);

    av_freep(&frame);
}

static AVBufferRef *nvtegra_pool_alloc(void *opaque, size_t size) {
    AVHWFramesContext        *ctx = opaque;
    AVNVTegraDeviceContext *hwctx = &((NVTegraDevicePriv *)ctx->device_ctx->hwctx)->p;

    AVBufferRef *buffer = NULL;
    AVNVTegraFrame *frame = NULL;
    AVNVTegraMap *map = NULL;
    int err;

    av_log(ctx, AV_LOG_DEBUG, "Creating surface from NVTEGRA device\n");

    map = av_mallocz(sizeof(*map));
    if (!map)
        goto fail;

    frame = av_mallocz(sizeof(*frame));
    if (!map)
        goto fail;

    /*
     * Framebuffers are allocated as CPU-cacheable, since they might get copied from
     * during transfer operations. Cache management is done manually.
     */
    err = av_nvtegra_map_create(map, &hwctx->nvdec_channel, size, 0x100,
                                NVMAP_HEAP_CARVEOUT_GENERIC, NVMAP_HANDLE_CACHEABLE);
    if (err < 0)
        goto fail;

    /* Flush the CPU cache */
    av_nvtegra_map_cache_op(map, NVMAP_CACHE_OP_WB, av_nvtegra_map_get_addr(map),
                            av_nvtegra_map_get_size(map));

    frame->map_ref = av_buffer_create((uint8_t *)map, sizeof(*map), nvtegra_map_free, ctx, 0);
    if (!frame->map_ref)
        goto fail;

    buffer = av_buffer_create((uint8_t *)frame, sizeof(*frame), nvtegra_frame_free, ctx, 0);
    if (!buffer)
        goto fail;

    return buffer;

fail:
    av_log(ctx, AV_LOG_ERROR, "Failed to create buffer\n");
    nvtegra_frame_free(opaque, (uint8_t *)frame);
    return NULL;
}

static int nvtegra_frames_init(AVHWFramesContext *ctx) {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(ctx->sw_format);

    uint32_t width_aligned, height_aligned, size;

    av_log(ctx, AV_LOG_DEBUG, "Initializing frame pool for the NVTEGRA device\n");

    if (!ctx->pool) {
        width_aligned  = FFALIGN(ctx->width,  nvtegra_surface_get_width_align (ctx->format, &desc->comp[0]));
        height_aligned = FFALIGN(ctx->height, nvtegra_surface_get_height_align(ctx->format, &desc->comp[0]));

        size = av_image_get_buffer_size(ctx->sw_format, width_aligned, height_aligned,
                                        nvtegra_surface_get_width_align(ctx->format, &desc->comp[0]));

        ffhwframesctx(ctx)->pool_internal = av_buffer_pool_init2(size, ctx, nvtegra_pool_alloc, NULL);
        if (!ffhwframesctx(ctx)->pool_internal)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static void nvtegra_frames_uninit(AVHWFramesContext *ctx) {
    av_log(ctx, AV_LOG_DEBUG, "Deinitializing frame pool for the NVTEGRA device\n");
}

static int nvtegra_get_buffer(AVHWFramesContext *ctx, AVFrame *frame) {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(ctx->sw_format);

    AVNVTegraMap *map;
    uint32_t width_aligned, height_aligned;
    int err;

    av_log(ctx, AV_LOG_DEBUG, "Getting frame buffer for NVTEGRA device\n");

    frame->buf[0] = av_buffer_pool_get(ctx->pool);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    map = av_nvtegra_frame_get_fbuf_map(frame);

    width_aligned  = FFALIGN(ctx->width,  nvtegra_surface_get_width_align (ctx->format, &desc->comp[0]));
    height_aligned = FFALIGN(ctx->height, nvtegra_surface_get_height_align(ctx->format, &desc->comp[0]));

    err = av_image_fill_arrays(frame->data, frame->linesize, av_nvtegra_map_get_addr(map),
                               ctx->sw_format, width_aligned, height_aligned,
                               nvtegra_surface_get_width_align(ctx->format, &desc->comp[0]));
    if (err < 0)
        return err;

    frame->format = AV_PIX_FMT_NVTEGRA;
    frame->width  = ctx->width;
    frame->height = ctx->height;

    return 0;
}

/*
 * Possible frequencies on Icosa and Mariko+, in MHz
 * (see tegra210-core-dvfs.c and tegra210b01-core-dvfs.c in l4t kernel sources, respectively):
 * for NVDEC:
 *   268.8, 384.0, 448.0, 486.4, 550.4, 576.0, 614.4, 652.8, 678.4, 691.2, 716.8
 *   460.8, 499.2, 556.8, 633.6, 652.8, 710.4, 748.8, 787.2, 825.6, 844.8, 883.2, 902.4, 921.6, 940.8, 960.0, 979.2
 * for NVJPG:
 *   192.0, 307.2, 345.6, 409.6, 486.4, 524.8, 550.4, 576.0, 588.8, 614.4, 627.2
 *   422.4, 441.6, 499.2, 518.4, 537.6, 556.8, 576.0, 595.2, 614.4, 633.6, 652.8
 */

int av_nvtegra_dfs_init(AVHWDeviceContext *ctx, AVNVTegraChannel *channel, int width, int height,
                        double framerate_hz)
{
    NVTegraDevicePriv *priv = ctx->hwctx;

    uint32_t max_freq, lowcorner;
    int num_mbs, err;

    priv->dfs_num_samples = 20;
    priv->dfs_ema_damping = 0.1;

    /*
     * Initialize low-corner frequency (reproduces official code)
     * Framerate might be unavailable (or variable), but this is official logic
     */
    num_mbs = width / 16 * height / 16;
    if (num_mbs <= 3600)
        lowcorner = 100000000;  /* 480p */
    else if (num_mbs <= 8160)
        lowcorner = 180000000;  /* 720p */
    else if (num_mbs <= 32400)
        lowcorner = 345000000;  /* 1080p */
    else
        lowcorner = 576000000;  /* 4k */

    if (framerate_hz >= 0.1 && isfinite(framerate_hz))
        lowcorner = FFMIN(lowcorner, lowcorner * framerate_hz / 30.0);

    priv->framerate     = framerate_hz;
    priv->dfs_lowcorner = lowcorner;

    av_log(ctx, AV_LOG_DEBUG, "DFS: Initializing lowcorner to %d Hz, using %u samples\n",
           priv->dfs_lowcorner, priv->dfs_num_samples);

    /*
     * Initialize channel to the max possible frequency (the kernel driver will clamp to an allowed value)
     * Note: Official code passes INT_MAX kHz then multiplies by 1000 (to Hz) and converts to u32,
     * resulting in this value.
     */
    max_freq = (UINT64_C(1)<<32) - 1000 & UINT32_MAX;

    err = nvtegra_channel_set_freq(channel, max_freq);
    if (err < 0)
        return err;

    priv->dfs_decode_cycles_ema = 0.0;
    priv->dfs_bitrate_sum       = 0;
    priv->dfs_cur_sample        = 0;
    priv->dfs_sampling_start_ts = av_gettime_relative();
    priv->dfs_last_ts_delta     = 0;

    return 0;
}

int av_nvtegra_dfs_update(AVHWDeviceContext *ctx, AVNVTegraChannel *channel, int bitstream_len, int decode_cycles) {
    NVTegraDevicePriv *priv = ctx->hwctx;

    double frame_time, avg;
    int64_t now, wl_dt;
    uint32_t clock;
    int err;

    /*
     * Official software implements DFS using a flat average of the decoder pool occupancy.
     * We instead use the decode cycles as reported by NVDEC microcode, and the "bitrate"
     * (bitstream bits fed to the hardware in a given clock time interval, NOT video time),
     * to calculate a suitable frequency, and multiply it by 1.2 for good measure:
     *   Freq = decode_cycles_per_bit * bits_per_second * 1.2
     */

    /* Convert to bits */
    bitstream_len *= 8;

    /* Exponential moving average of decode cycles per frame */
    priv->dfs_decode_cycles_ema = priv->dfs_ema_damping * (double)decode_cycles/bitstream_len +
        (1.0 - priv->dfs_ema_damping) * priv->dfs_decode_cycles_ema;

    priv->dfs_bitrate_sum += bitstream_len;
    priv->dfs_cur_sample   = (priv->dfs_cur_sample + 1) % priv->dfs_num_samples;

    err = 0;

    /* Reclock if we collected enough samples */
    if (priv->dfs_cur_sample == 0) {
        now   = av_gettime_relative();
        wl_dt = now - priv->dfs_sampling_start_ts;

        /*
         * Try to filter bad sample sets caused by eg. pausing the video playback.
         * We reject if one of these conditions is met:
         * - the wall time is over 1.5x the framerate (10Hz is used as fallback if no framerate information is available)
         * - the wall time is over 1.5x the ema-damped previous values
         */

        if (priv->framerate >= 0.1 && isfinite(priv->framerate))
            frame_time = 1.0e6 / priv->framerate;
        else
            frame_time = 0.1e6;

        if ((wl_dt < 1.5 * priv->dfs_num_samples * frame_time) ||
                ((priv->dfs_last_ts_delta) && (wl_dt < 1.5 * priv->dfs_last_ts_delta))) {
            avg   = priv->dfs_bitrate_sum * 1e6 / wl_dt;
            clock = priv->dfs_decode_cycles_ema * avg * 1.2;
            clock = FFMAX(clock, priv->dfs_lowcorner);

            av_log(ctx, AV_LOG_DEBUG, "DFS: %.0f cycles/b (ema), %.0f b/s -> clock %u Hz (lowcorner %u Hz)\n",
                priv->dfs_decode_cycles_ema, avg, clock, priv->dfs_lowcorner);

            err = nvtegra_channel_set_freq(channel, clock);

            priv->dfs_last_ts_delta = wl_dt;
        }

        priv->dfs_bitrate_sum       = 0;
        priv->dfs_sampling_start_ts = now;
    }

    return err;
}

int av_nvtegra_dfs_uninit(AVHWDeviceContext *ctx, AVNVTegraChannel *channel) {
    return nvtegra_channel_set_freq(channel, 0);
}

static int nvtegra_transfer_get_formats(AVHWFramesContext *ctx,
                                        enum AVHWFrameTransferDirection dir,
                                        enum AVPixelFormat **formats)
{
    enum AVPixelFormat *fmts;

    av_log(ctx, AV_LOG_DEBUG, "Getting transfer formats for NVTEGRA device\n");

    fmts = av_malloc_array(2, sizeof(**formats));
    if (!fmts)
        return AVERROR(ENOMEM);

    fmts[0] = ctx->sw_format;
    fmts[1] = AV_PIX_FMT_NONE;

    *formats = fmts;
    return 0;
}

static inline void nvtegra_cpu_copy_plane(void *dst, int dst_stride,
                                          void *src, int src_stride, int h, bool from)
{
    /*
     * Adapted from https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/.
     * We process 16x2 bytes at a time. Horizontally, this is the size of a linear atom
     * in a 16Bx2 sector, conveniently also the size of a cache line and of a macroblock.
     *
     * NVDEC always uses a GOB height of 2 (block height of 16, in line with macroblock dimensions).
     * The corresponding swizzling pattern is the following:
     *    y3 y2 y1 y0 x5 x4 x3 x2 x1 x0
     * x: ___x5_______x4____x3 x3 x1 x0
     * y: y3____y2 y1____y0____________
     *
     * Addresses for the 4 lower bits can then be copied as-is (16 bytes).
     * As a further optimization, the y0 bit is also handled within the same inner loop,
     * which halves the total number of iterations.
     *
     * This function is declared inline with the expectation that the compiler will optimize
     * the branches depending on the copy direction.
     */

    __uint128_t *src_ = src, *dst_ = dst, *src_line, *dst_line;
    uint32_t ws = src_stride / sizeof(__uint128_t), wd = dst_stride / sizeof(__uint128_t),
        w = FFMIN(ws, wd), offs_x = 0, offs_y = 0, offs_line;
    uint32_t x_mask = -0x2e, y_mask = 0x2c;
    int x, y;

    for (y = 0; y < h; y += 2) {
        dst_line = dst_ + (from ? y * wd : offs_y);
        src_line = src_ + (from ? offs_y : y * ws);

        offs_line = offs_x;
        for (x = 0; x < w; ++x) {
            dst_line[from ? x+0  : offs_line+0] = src_line[from ? offs_line+0 : x+0 ];
            dst_line[from ? x+wd : offs_line+1] = src_line[from ? offs_line+1 : x+ws];
            offs_line = (offs_line - x_mask) & x_mask;
        }

        offs_y = (offs_y - y_mask) & y_mask;

        /* Wrap into next tile row */
        if (!offs_y)
            offs_x += from ? src_stride : dst_stride;
    }
}

static int nvtegra_cpu_transfer_data(AVHWFramesContext *ctx, const AVFrame *dst, const AVFrame *src,
                                     int num_planes, bool from)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(ctx->sw_format);
    const AVFrame *hwframe, *swframe;
    AVNVTegraMap *map;
    int h, i;

    hwframe = from ? src : dst, swframe = from ? dst : src;
    map = av_nvtegra_frame_get_fbuf_map(hwframe);

    if (swframe->format != ctx->sw_format) {
        av_log(ctx, AV_LOG_ERROR, "Source and destination must have the same format for cpu transfers\n");
        return AVERROR(EINVAL);
    }

    /* If we are transferring from a hardware frame, invalidate the CPU cache which might be stale */
    if (from) {
        av_nvtegra_map_cache_op(map, NVMAP_CACHE_OP_INV,
                                av_nvtegra_map_get_addr(map), av_nvtegra_map_get_size(map));
    }

    /* Align the height to an even size */
    h = FFALIGN(dst->height, 2);

    for (i = 0; i < num_planes; ++i) {
        if (map->is_linear) {
            av_image_copy_plane(dst->data[i], dst->linesize[i], src->data[i], src->linesize[i],
                                FFMIN(dst->linesize[i], src->linesize[i]),
                                h >> (i ? desc->log2_chroma_h : 0));
        } else {
            /*
             * Instanciate the same inlined function for both destinations,
             * giving the compiler the opportunity to remove branching within the copy loops.
             * (verified by decompilation at -O1 and higher for both gcc and clang)
             */
            if (from)
                nvtegra_cpu_copy_plane(dst->data[i], dst->linesize[i], src->data[i], src->linesize[i],
                                       h >> (i ? desc->log2_chroma_h : 0), true);
            else
                nvtegra_cpu_copy_plane(dst->data[i], dst->linesize[i], src->data[i], src->linesize[i],
                                       h >> (i ? desc->log2_chroma_h : 0), false);
        }
    }

    /* If we transferred to a hardware frame, flush the CPU cache to make the data visible to hardware engines */
    if (!from) {
        av_nvtegra_map_cache_op(map, NVMAP_CACHE_OP_WB,
                                av_nvtegra_map_get_addr(map), av_nvtegra_map_get_size(map));
    }

    return 0;
}

static void nvtegra_vic_preprare_config(VicConfigStruct *config, const AVFrame *src, const AVFrame *dst,
                                        enum AVPixelFormat fmt, bool is_16b_chroma)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    bool input_linear = (src->format != AV_PIX_FMT_NVTEGRA) || av_nvtegra_frame_get_fbuf_map(src)->is_linear,
        output_linear = (dst->format != AV_PIX_FMT_NVTEGRA) || av_nvtegra_frame_get_fbuf_map(dst)->is_linear;

    /*
     * The VIC engine has an undocumented limitation regarding height alignment,
     * which should be padded to an even size.
     */

    /* Subsampled dimensions when emulating 16-bit chroma transfers, as input is always NV12 */
    int divider   = !is_16b_chroma ? 1 : 2;
    int src_width = src->width / divider, src_height = FFALIGN(src->height, 2) / divider,
        dst_width = dst->width / divider, dst_height = FFALIGN(dst->height, 2) / divider;

    *config = (VicConfigStruct){
        .pipeConfig = {
            .DownsampleHoriz            = 1 << 2, /* U9.2 */
            .DownsampleVert             = 1 << 2, /* U9.2 */
        },
        .outputConfig = {
            .AlphaFillMode              = !is_16b_chroma ? NVB0B6_DXVAHD_ALPHA_FILL_MODE_OPAQUE :
                                                           NVB0B6_DXVAHD_ALPHA_FILL_MODE_SOURCE_STREAM,
            .BackgroundAlpha            = 0,
            .BackgroundR                = 0,
            .BackgroundG                = 0,
            .BackgroundB                = 0,
            .TargetRectLeft             = 0,
            .TargetRectRight            = dst_width  - 1,
            .TargetRectTop              = 0,
            .TargetRectBottom           = dst_height - 1,
        },
        .outputSurfaceConfig = {
            .OutPixelFormat             = av_nvtegra_pixfmt_to_vic(fmt),
            .OutSurfaceWidth            = dst_width  - 1,
            .OutSurfaceHeight           = dst_height - 1,
            .OutBlkKind                 = !output_linear ? NVB0B6_BLK_KIND_GENERIC_16Bx2 : NVB0B6_BLK_KIND_PITCH,
            .OutBlkHeight               = !output_linear ? 1 : 0, /* GOB height 2 */
            .OutLumaWidth               = (dst->linesize[0] / desc->comp[0].step) - 1,
            .OutLumaHeight              = FFALIGN(dst_height, !output_linear ? 32 : 2) - 1,
            .OutChromaWidth             = (desc->flags & AV_PIX_FMT_FLAG_RGB) ?
                                          -1 : (dst->linesize[1] / desc->comp[1].step) - 1,
            .OutChromaHeight            = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? -1 :
                                          (FFALIGN(dst_height, !output_linear ? 32 : 2) >> desc->log2_chroma_h) - 1,
        },
        .slotStruct = {
            {
                .slotConfig = {
                    .SlotEnable         = 1,
                    .CurrentFieldEnable = 1,
                    .SoftClampLow       = 0,
                    .SoftClampHigh      = 1023,
                    .PlanarAlpha        = 1023,
                    .ConstantAlpha      = 1,
                    .SourceRectLeft     = 0,
                    .SourceRectRight    = (src_width  - 1) << 16, /* U14.16 (for subpixel positioning) */
                    .SourceRectTop      = 0,
                    .SourceRectBottom   = (src_height - 1) << 16,
                    .DestRectLeft       = 0,
                    .DestRectRight      = src_width  - 1,
                    .DestRectTop        = 0,
                    .DestRectBottom     = src_height - 1,
                },
                .slotSurfaceConfig = {
                    .SlotPixelFormat    = av_nvtegra_pixfmt_to_vic(fmt),
                    .SlotChromaLocHoriz = ((desc->flags & AV_PIX_FMT_FLAG_RGB)          ||
                                           src->chroma_location == AVCHROMA_LOC_TOPLEFT ||
                                           src->chroma_location == AVCHROMA_LOC_LEFT    ||
                                           src->chroma_location == AVCHROMA_LOC_BOTTOMLEFT) ? 0 : 1,
                    .SlotChromaLocVert  = ((desc->flags & AV_PIX_FMT_FLAG_RGB)          ||
                                           src->chroma_location == AVCHROMA_LOC_TOPLEFT ||
                                           src->chroma_location == AVCHROMA_LOC_TOP) ? 0 :
                                          (src->chroma_location == AVCHROMA_LOC_LEFT ||
                                           src->chroma_location == AVCHROMA_LOC_CENTER) ? 1 : 2,
                    .SlotBlkKind        = !input_linear ? NVB0B6_BLK_KIND_GENERIC_16Bx2 : NVB0B6_BLK_KIND_PITCH,
                    .SlotBlkHeight      = !input_linear ? 1 : 0, /* GOB height 2 */
                    .SlotCacheWidth     = !input_linear ? 1 : 3, /* 32Bx8 for block, 128Bx2 for pitch */
                    .SlotSurfaceWidth   = src_width  - 1,
                    .SlotSurfaceHeight  = src_height - 1,
                    .SlotLumaWidth      = (src->linesize[0] / desc->comp[0].step) - 1,
                    .SlotLumaHeight     = FFALIGN(src_height, !input_linear ? 32 : 2) - 1,
                    .SlotChromaWidth    = (desc->flags & AV_PIX_FMT_FLAG_RGB) ?
                                          -1 : (src->linesize[1] / desc->comp[1].step) - 1,
                    .SlotChromaHeight   = (desc->flags & AV_PIX_FMT_FLAG_RGB) ? -1 :
                                          (FFALIGN(src_height, !input_linear ? 32 : 2) >> desc->log2_chroma_h) - 1,
                },
            },
        },
    };
}

static int nvtegra_vic_prepare_cmdbuf(AVHWFramesContext *ctx, AVNVTegraJobPool *pool, AVNVTegraJob *job,
                                      const AVFrame *src, const AVFrame *dst, enum AVPixelFormat fmt,
                                      AVNVTegraMap **plane_maps, uint32_t *plane_offsets, int num_planes)
{
    NVTegraDevicePriv *priv = ctx->device_ctx->hwctx;
    AVNVTegraCmdbuf *cmdbuf = &job->cmdbuf;

    AVNVTegraMap *src_maps[4], *dst_maps[4];
    uint32_t src_map_offsets[4], dst_map_offsets[4];
    int src_reloc_type, dst_reloc_type, i, err;

#define RELOC_VARS(frame) ({                                                             \
    if (frame->format == AV_PIX_FMT_NVTEGRA) {                                           \
        for (i = 0; i < FF_ARRAY_ELEMS(AV_JOIN(frame, _map_offsets)); ++i) {             \
            AV_JOIN(frame, _maps       )[i] = av_nvtegra_frame_get_fbuf_map(frame);      \
            AV_JOIN(frame, _map_offsets)[i] = frame->data[i] - frame->data[0];           \
        }                                                                                \
        AV_JOIN(frame, _reloc_type) = !av_nvtegra_frame_get_fbuf_map(frame)->is_linear ? \
            NVHOST_RELOC_TYPE_BLOCK_LINEAR : NVHOST_RELOC_TYPE_PITCH_LINEAR;             \
    } else {                                                                             \
        for (i = 0; i < FF_ARRAY_ELEMS(AV_JOIN(frame, _map_offsets)); ++i) {             \
            AV_JOIN(frame, _maps       )[i] = plane_maps   [i];                          \
            AV_JOIN(frame, _map_offsets)[i] = plane_offsets[i];                          \
        }                                                                                \
        AV_JOIN(frame, _reloc_type) = NVHOST_RELOC_TYPE_PITCH_LINEAR;                    \
    }                                                                                    \
})

    RELOC_VARS(src);
    RELOC_VARS(dst);

    err = av_nvtegra_cmdbuf_begin(cmdbuf, HOST1X_CLASS_VIC);
    if (err < 0)
        return err;

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS,
                          AV_NVTEGRA_VALUE(NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS, CONFIG_STRUCT_SIZE, sizeof(VicConfigStruct) >> 4) |
                          AV_NVTEGRA_VALUE(NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS, GPTIMER_ON,         1)                            |
                          AV_NVTEGRA_VALUE(NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS, FALCON_CONTROL,     1));
    AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_SET_CONFIG_STRUCT_OFFSET,
                          &job->input_map, priv->vic_setup_off, NVHOST_RELOC_TYPE_DEFAULT);

    switch (fmt) {
        /* 16-bit transfer emulation */
        case AV_PIX_FMT_RGB565:
            /* Luma transfer */
            AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_LUMA_OFFSET(0),
                                  src_maps[0], src_map_offsets[0], src_reloc_type);
            AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_LUMA_OFFSET,
                                  dst_maps[0], dst_map_offsets[0], dst_reloc_type);
            break;
        case AV_PIX_FMT_RGB32:
            /* Chroma transfer */
            AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_LUMA_OFFSET(0),
                                  src_maps[1], src_map_offsets[1], src_reloc_type);
            AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_LUMA_OFFSET,
                                  dst_maps[1], dst_map_offsets[1], dst_reloc_type);
            break;

        /* Normal transfers */
        case AV_PIX_FMT_GRAY8:
        case AV_PIX_FMT_NV12:
        case AV_PIX_FMT_YUV420P:
            for (i = 0; i < num_planes; ++i) {
                AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_LUMA_OFFSET(0)    + i * sizeof(uint32_t),
                                      src_maps[i], src_map_offsets[i], src_reloc_type);
                AV_NVTEGRA_PUSH_RELOC(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_LUMA_OFFSET + i * sizeof(uint32_t),
                                      dst_maps[i], dst_map_offsets[i], dst_reloc_type);
            }
            break;
        default:
            return AVERROR(EINVAL);
    }

    AV_NVTEGRA_PUSH_VALUE(cmdbuf, NVB0B6_VIDEO_COMPOSITOR_EXECUTE,
                          AV_NVTEGRA_ENUM(NVB0B6_VIDEO_COMPOSITOR_EXECUTE, AWAKEN, ENABLE));

    err = av_nvtegra_cmdbuf_add_syncpt_incr(cmdbuf, pool->channel->syncpt, 0);
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_end(cmdbuf);
    if (err < 0)
        return err;

    return 0;
}

static int nvtegra_vic_copy_plane(AVHWFramesContext *ctx, AVNVTegraJob *job,
                                  const AVFrame *src, const AVFrame *dst,
                                  enum AVPixelFormat fmt, AVNVTegraMap **plane_maps, uint32_t *plane_offsets,
                                  int num_planes, bool is_chroma)
{
    NVTegraDevicePriv *priv = ctx->device_ctx->hwctx;

    uint8_t *mem;
    int err;

    mem = av_nvtegra_map_get_addr(&job->input_map);

    nvtegra_vic_preprare_config((VicConfigStruct *)(mem + priv->vic_setup_off),
                                src, dst, fmt, is_chroma);

    err = av_nvtegra_cmdbuf_clear(&job->cmdbuf);
    if (err < 0)
        return err;

    err = nvtegra_vic_prepare_cmdbuf(ctx, &priv->job_pool, job, src, dst, fmt,
                                     plane_maps, plane_offsets, num_planes);
    if (err < 0)
        goto fail;

    err = av_nvtegra_job_submit(&priv->job_pool, job);
    if (err < 0)
        goto fail;

    err = av_nvtegra_job_wait(&priv->job_pool, job, -1);
    if (err < 0)
        goto fail;

fail:
    return err;
}

static int nvtegra_vic_transfer_data(AVHWFramesContext *ctx, const AVFrame *dst, const AVFrame *src,
                                     int num_planes, bool from)
{
    NVTegraDevicePriv       *priv = ctx->device_ctx->hwctx;
    AVNVTegraDeviceContext *hwctx = &priv->p;

    AVBufferRef *job_ref;
    AVNVTegraJob *job;
    const AVFrame *swframe;
    uint8_t *map_bases[4];
    AVNVTegraMap maps[4] = {0};
    AVNVTegraMap *plane_maps[4];
    uint32_t plane_offsets[4];
    int num_maps, i, j, err;

    swframe = from ? dst : src;

    job_ref = av_nvtegra_job_pool_get(&priv->job_pool);
    if (!job_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    job = (AVNVTegraJob *)job_ref->data;

    /* Create a map for each frame backing buffer */
    for (i = 0; i < FF_ARRAY_ELEMS(maps); num_maps = ++i) {
        if (!swframe->buf[i])
            break;

        /*
         * In order to avoid a full-frame copy on the CPU, the provided memory
         * is mapped into VIC and used directly during the transfer.
         * The address and size are aligned to page boundaries.
         * Cache management is performed manually to not affect data outside the buffer.
         */
        map_bases[i] = (uint8_t *)((uintptr_t)swframe->buf[i]->data & ~0xfff);
        err = av_nvtegra_map_from_va(&maps[i], &hwctx->vic_channel, map_bases[i],
                                     swframe->buf[i]->size + ((uintptr_t)swframe->buf[i]->data & 0xfff),
                                     0x100, NVMAP_HANDLE_CACHEABLE);
        if (err < 0)
            goto fail;

        err = av_nvtegra_map_map(&maps[i]);
        if (err < 0)
            goto fail;

        /* Flush-invalidate the CPU cache prior to the transfer */
        av_nvtegra_map_cache_op(&maps[i], NVMAP_CACHE_OP_WB_INV,
                                ((uint8_t *)av_nvtegra_map_get_addr(&maps[i])) +
                                    ((uintptr_t)swframe->buf[i]->data & 0xfff),
                                swframe->buf[i]->size);
    }

    /* Find the corresponding map object and its offset for each plane  */
    for (i = 0; i < num_planes; ++i) {
        for (j = 0; j < FF_ARRAY_ELEMS(swframe->buf); ++j) {
            if ((swframe->buf[j]->data <= swframe->data[i]) &&
                    (swframe->data[i] < swframe->buf[j]->data + swframe->buf[j]->size))
                break;
        }

        plane_maps   [i] = &maps[j];
        plane_offsets[i] = swframe->data[i] - map_bases[j];
    }

    /* VIC expects planes in the reversed order */
    if (swframe->format == AV_PIX_FMT_YUV420P) {
        FFSWAP(AVNVTegraMap *, plane_maps   [1], plane_maps   [2]);
        FFSWAP(uint32_t,       plane_offsets[1], plane_offsets[2]);
    }

    /*
     * VIC2 does not support 16-bit YUV surfaces (eg. P010, P012, ...).
     * Here we emulate them using two separates transfers for the luma and chroma planes
     * (16-bit and 32-bit widths respectively).
     */
    if (swframe->format == AV_PIX_FMT_P010) {
        err = nvtegra_vic_copy_plane(ctx, job, src, dst, AV_PIX_FMT_RGB565,
                                     plane_maps, plane_offsets, 1, false);
        if (err < 0)
            goto fail;

        err = nvtegra_vic_copy_plane(ctx, job, src, dst, AV_PIX_FMT_RGB32,
                                     plane_maps, plane_offsets, 1, true);
        if (err < 0)
            goto fail;
    } else {
        err = nvtegra_vic_copy_plane(ctx, job, src, dst, swframe->format,
                                     plane_maps, plane_offsets, num_planes, false);
        if (err < 0)
            goto fail;
    }

fail:
    for (i = 0; i < num_maps; ++i) {
        av_nvtegra_map_unmap(&maps[i]);
        av_nvtegra_map_close(&maps[i]);
    }

    av_buffer_unref(&job_ref);

    return err;
}

static int nvtegra_transfer_data(AVHWFramesContext *ctx, AVFrame *dst, const AVFrame *src) {
    const AVFrame *swframe;
    bool from;
    int num_planes, i;

    from    = !dst->hw_frames_ctx;
    swframe = from ? dst : src;

    if (swframe->hw_frames_ctx)
        return AVERROR(ENOSYS);

    num_planes = av_pix_fmt_count_planes(swframe->format);

    for (i = 0; i < num_planes; ++i) {
        if (((uintptr_t)swframe->data[i] & 0xff) || (swframe->linesize[i] & 0xff)) {
            av_log(ctx, AV_LOG_WARNING, "Frame address/pitch not aligned to 256, "
                                        "falling back to cpu transfer\n");
            return nvtegra_cpu_transfer_data(ctx, dst, src, num_planes, from);
        }
    }

    return nvtegra_vic_transfer_data(ctx, dst, src, num_planes, from);
}

const HWContextType ff_hwcontext_type_nvtegra = {
    .type                   = AV_HWDEVICE_TYPE_NVTEGRA,
    .name                   = "nvtegra",

    .device_hwctx_size      = sizeof(NVTegraDevicePriv),
    .device_hwconfig_size   = 0,
    .frames_hwctx_size      = 0,

    .device_create          = &nvtegra_device_create,
    .device_init            = &nvtegra_device_init,
    .device_uninit          = &nvtegra_device_uninit,

    .frames_get_constraints = &nvtegra_frames_get_constraints,
    .frames_init            = &nvtegra_frames_init,
    .frames_uninit          = &nvtegra_frames_uninit,
    .frames_get_buffer      = &nvtegra_get_buffer,

    .transfer_get_formats   = &nvtegra_transfer_get_formats,
    .transfer_data_to       = &nvtegra_transfer_data,
    .transfer_data_from     = &nvtegra_transfer_data,

    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_NVTEGRA,
        AV_PIX_FMT_NONE,
    },
};
