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

#ifndef __SWITCH__
#   include <sys/ioctl.h>
#   include <sys/mman.h>
#   include <fcntl.h>
#   include <unistd.h>
#else
#   include <stdlib.h>
#   include <switch.h>
#endif

#include <string.h>

#include "buffer.h"
#include "log.h"
#include "error.h"
#include "mem.h"
#include "thread.h"

#include "nvhost_ioctl.h"
#include "nvmap_ioctl.h"
#include "nvtegra_host1x.h"

#include "nvtegra.h"

/*
 * Tag used by the kernel to identify allocations.
 * Official software has been seen using 0x900, 0xf00, 0x1100, 0x1400, 0x4000.
 */
#define MEM_TAG (0xfeed)

struct DriverState {
    int nvmap_fd, nvhost_fd;
};

static AVMutex g_driver_init_mtx = AV_MUTEX_INITIALIZER;
static struct DriverState *g_driver_state = NULL;
static AVBufferRef *g_driver_state_ref = NULL;

static void free_driver_fds(void *opaque, uint8_t *data) {
    if (!g_driver_state)
        return;

#ifndef __SWITCH__
    if (g_driver_state->nvmap_fd > 0)
        close(g_driver_state->nvmap_fd);

    if (g_driver_state->nvhost_fd > 0)
        close(g_driver_state->nvhost_fd);
#else
    nvFenceExit();
    nvMapExit();
    nvExit();
    mmuExit();
#endif

    g_driver_init_mtx  = (AVMutex)AV_MUTEX_INITIALIZER;
    g_driver_state_ref = NULL;
    av_freep(&g_driver_state);
}

static int init_driver_fds(void) {
    AVBufferRef *ref;
    struct DriverState *state;
    int err;

    state = av_mallocz(sizeof(*state));
    if (!state)
        return AVERROR(ENOMEM);

    ref = av_buffer_create((uint8_t *)state, sizeof(*state), free_driver_fds, NULL, 0);
    if (!state)
        return AVERROR(ENOMEM);

    g_driver_state     = state;
    g_driver_state_ref = ref;

#ifndef __SWITCH__
    err = open("/dev/nvmap", O_RDWR | O_SYNC);
    if (err < 0)
        return AVERROR(errno);
    state->nvmap_fd = err;

    err = open("/dev/nvhost-ctrl", O_RDWR | O_SYNC);
    if (err < 0)
        return AVERROR(errno);
    state->nvhost_fd = err;
#else
    err = nvInitialize();
    if (R_FAILED(err))
        return AVERROR(err);

    err = nvMapInit();
    if (R_FAILED(err))
        return AVERROR(err);
    state->nvmap_fd = nvMapGetFd();

    err = nvFenceInit();
    if (R_FAILED(err))
        return AVERROR(err);
    /* libnx doesn't export the nvhost-ctrl file descriptor */

    err = mmuInitialize();
    if (R_FAILED(err))
        return AVERROR(err);
#endif

    return 0;
}

static inline int get_nvmap_fd(void) {
    if (!g_driver_state)
        return AVERROR_UNKNOWN;

    if (!g_driver_state->nvmap_fd)
        return AVERROR_UNKNOWN;

    return g_driver_state->nvmap_fd;
}

static inline int get_nvhost_fd(void) {
    if (!g_driver_state)
        return AVERROR_UNKNOWN;

    if (!g_driver_state->nvhost_fd)
        return AVERROR_UNKNOWN;

    return g_driver_state->nvhost_fd;
}

AVBufferRef *av_nvtegra_driver_init(void) {
    AVBufferRef *out = NULL;
    int err;

    /*
     * We have to do this overly complex dance of putting driver fds in a refcounted struct,
     * otherwise initializing multiple hwcontexts would leak fds
     */

    err = ff_mutex_lock(&g_driver_init_mtx);
    if (err != 0)
        goto exit;

    if (g_driver_state_ref) {
        out = av_buffer_ref(g_driver_state_ref);
        goto exit;
    }

    err = init_driver_fds();
    if (err < 0) {
        /* In case memory allocations failed, call the destructor ourselves */
        av_buffer_unref(&g_driver_state_ref);
        free_driver_fds(NULL, NULL);
        goto exit;
    }

    out = g_driver_state_ref;

exit:
    ff_mutex_unlock(&g_driver_init_mtx);
    return out;
}

int av_nvtegra_channel_open(AVNVTegraChannel *channel, const char *dev) {
    int err;
#ifndef __SWITCH__
    struct nvhost_get_param_arg args;

    err = open(dev, O_RDWR);
    if (err < 0)
        return AVERROR(errno);

    channel->fd = err;

    args = (struct nvhost_get_param_arg){0};

    err = ioctl(channel->fd, NVHOST_IOCTL_CHANNEL_GET_SYNCPOINT, &args);
    if (err < 0)
        goto fail;

    channel->syncpt = args.value;

    return 0;

fail:
    close(channel->fd);
    return AVERROR(errno);
#else
    err = nvChannelCreate(&channel->channel, dev);
    if (R_FAILED(err))
        return AVERROR(err);

    err = nvioctlChannel_GetSyncpt(channel->channel.fd, 0, &channel->syncpt);
    if (R_FAILED(err))
        goto fail;

    return 0;

fail:
    nvChannelClose(&channel->channel);
    return AVERROR(err);
#endif
}

int av_nvtegra_channel_close(AVNVTegraChannel *channel) {
#ifndef __SWITCH__
    if (!channel->fd)
        return 0;

    return close(channel->fd);
#else
    nvChannelClose(&channel->channel);
    return 0;
#endif
}

int av_nvtegra_channel_get_clock_rate(AVNVTegraChannel *channel, uint32_t moduleid, uint32_t *clock_rate) {
    int err;
#ifndef __SWITCH__
    struct nvhost_clk_rate_args args;

    args = (struct nvhost_clk_rate_args){
        .moduleid = moduleid,
    };

    err = ioctl(channel->fd, NVHOST_IOCTL_CHANNEL_GET_CLK_RATE, &args);
    if (err < 0)
        return AVERROR(errno);

    if (clock_rate)
        *clock_rate = args.rate;

    return 0;
#else
    uint32_t tmp;

    err = AVERROR(nvioctlChannel_GetModuleClockRate(channel->channel.fd, moduleid, &tmp));
    if (err < 0)
        return err;

    if (clock_rate)
        *clock_rate = tmp * 1000;

    return 0;
#endif
}

int av_nvtegra_channel_set_clock_rate(AVNVTegraChannel *channel, uint32_t moduleid, uint32_t clock_rate) {
#ifndef __SWITCH__
    struct nvhost_clk_rate_args args;

    args = (struct nvhost_clk_rate_args){
        .rate     = clock_rate,
        .moduleid = moduleid,
    };

    return (ioctl(channel->fd, NVHOST_IOCTL_CHANNEL_SET_CLK_RATE, &args) < 0) ? AVERROR(errno) : 0;
#else
    return AVERROR(nvioctlChannel_SetModuleClockRate(channel->channel.fd, moduleid, clock_rate / 1000));
#endif
}

int av_nvtegra_channel_submit(AVNVTegraChannel *channel, AVNVTegraCmdbuf *cmdbuf, uint32_t *fence) {
    int err;
#ifndef __SWITCH__
    struct nvhost_submit_args args;

    args = (struct nvhost_submit_args){
        .submit_version          = NVHOST_SUBMIT_VERSION_V2,
        .num_syncpt_incrs        = cmdbuf->num_syncpt_incrs,
        .num_cmdbufs             = cmdbuf->num_cmdbufs,
        .num_relocs              = cmdbuf->num_relocs,
        .num_waitchks            = cmdbuf->num_waitchks,
        .timeout                 = 0,
        .flags                   = 0,
        .fence                   = 0,
        .syncpt_incrs            = (uintptr_t)cmdbuf->syncpt_incrs,
        .cmdbuf_exts             = (uintptr_t)cmdbuf->cmdbuf_exts,
        .checksum_methods        = 0,
        .checksum_falcon_methods = 0,
        .pad                     = { 0 },
        .reloc_types             = (uintptr_t)cmdbuf->reloc_types,
        .cmdbufs                 = (uintptr_t)cmdbuf->cmdbufs,
        .relocs                  = (uintptr_t)cmdbuf->relocs,
        .reloc_shifts            = (uintptr_t)cmdbuf->reloc_shifts,
        .waitchks                = (uintptr_t)cmdbuf->waitchks,
        .waitbases               = 0,
        .class_ids               = (uintptr_t)cmdbuf->class_ids,
        .fences                  = (uintptr_t)cmdbuf->fences,
    };

    err = ioctl(channel->fd, NVHOST_IOCTL_CHANNEL_SUBMIT, &args);
    if (err < 0)
        return AVERROR(errno);

    if (fence)
        *fence = args.fence;

    return 0;
#else
    nvioctl_fence tmp;

    err = nvioctlChannel_Submit(channel->channel.fd, (nvioctl_cmdbuf *)cmdbuf->cmdbufs, cmdbuf->num_cmdbufs,
                                NULL, NULL, 0, (nvioctl_syncpt_incr *)cmdbuf->syncpt_incrs, cmdbuf->num_syncpt_incrs,
                                &tmp, 1);
    if (R_FAILED(err))
        return AVERROR(err);

    if (fence)
        *fence = tmp.value;

    return 0;
#endif
}

int av_nvtegra_channel_set_submit_timeout(AVNVTegraChannel *channel, uint32_t timeout_ms) {
#ifndef __SWITCH__
    struct nvhost_set_timeout_args args;

    args = (struct nvhost_set_timeout_args){
        .timeout = timeout_ms,
    };

    return (ioctl(channel->fd, NVHOST_IOCTL_CHANNEL_SET_TIMEOUT, &args) < 0) ? AVERROR(errno) : 0;
#else
    return AVERROR(nvioctlChannel_SetSubmitTimeout(channel->channel.fd, timeout_ms));
#endif
}

int av_nvtegra_syncpt_wait(AVNVTegraChannel *channel, uint32_t threshold, int32_t timeout) {
#ifndef __SWITCH__
    struct nvhost_ctrl_syncpt_waitex_args args = {
        .id      = channel->syncpt,
        .thresh  = threshold,
        .timeout = timeout,
    };

    return (ioctl(get_nvhost_fd(), NVHOST_IOCTL_CTRL_SYNCPT_WAITEX, &args) < 0) ? AVERROR(errno) : 0;
#else
    NvFence fence;

    fence = (NvFence){
        .id    = channel->syncpt,
        .value = threshold,
    };

    return AVERROR(nvFenceWait(&fence, timeout));
#endif
}

#ifdef __SWITCH__
static inline bool convert_cache_flags(uint32_t flags) {
    /* Return whether the map should be CPU-cacheable */
    switch (flags & NVMAP_HANDLE_CACHE_FLAG) {
        case NVMAP_HANDLE_INNER_CACHEABLE:
        case NVMAP_HANDLE_CACHEABLE:
            return true;
        default:
            return false;
    }
}
#endif

int av_nvtegra_map_allocate(AVNVTegraMap *map, AVNVTegraChannel *channel, uint32_t size,
                            uint32_t align, int heap_mask, int flags)
{
#ifndef __SWITCH__
    struct nvmap_create_handle create_args;
    struct nvmap_alloc_handle alloc_args;
    int err;

    create_args = (struct nvmap_create_handle){
        .size   = size,
    };

    err = ioctl(get_nvmap_fd(), NVMAP_IOC_CREATE, &create_args);
    if (err < 0)
        return AVERROR(errno);

    map->size   = size;
    map->handle = create_args.handle;

    alloc_args = (struct nvmap_alloc_handle){
        .handle    = create_args.handle,
        .heap_mask = heap_mask,
        .flags     = flags | (MEM_TAG << 16),
        .align     = align,
    };

    err = ioctl(get_nvmap_fd(), NVMAP_IOC_ALLOC, &alloc_args);
    if (err < 0)
        goto fail;

    return 0;

fail:
    av_nvtegra_map_free(map);
    return AVERROR(errno);
#else
    void *mem;

    map->owner = channel->channel.fd;

    size = FFALIGN(size, 0x1000);

    mem = aligned_alloc(FFALIGN(align, 0x1000), size);
    if (!mem)
        return AVERROR(ENOMEM);

    return AVERROR(nvMapCreate(&map->map, mem, size, 0x10000, NvKind_Pitch,
                               convert_cache_flags(flags)));
#endif
}

int av_nvtegra_map_free(AVNVTegraMap *map) {
#ifndef __SWITCH__
    int err;

    if (!map->handle)
        return 0;

    err = ioctl(get_nvmap_fd(), NVMAP_IOC_FREE, map->handle);
    if (err < 0)
        return AVERROR(errno);

    map->handle = 0;

    return 0;
#else
    void *addr = map->map.cpu_addr;

    if (!map->map.cpu_addr)
        return 0;

    nvMapClose(&map->map);
    free(addr);
    return 0;
#endif
}

int av_nvtegra_map_from_va(AVNVTegraMap *map, AVNVTegraChannel *owner, void *mem,
                           uint32_t size, uint32_t align, uint32_t flags)
{
#ifndef __SWITCH__
    struct nvmap_create_handle_from_va args;
    int err;

    args = (struct nvmap_create_handle_from_va){
        .va    = (uintptr_t)mem,
        .size  = size,
        .flags = flags | (MEM_TAG << 16),
    };

    err = ioctl(get_nvmap_fd(), NVMAP_IOC_FROM_VA, &args);
    if (err < 0)
        return AVERROR(errno);

    map->cpu_addr = mem;
    map->size     = size;
    map->handle   = args.handle;

    return 0;
#else

    map->owner = owner->channel.fd;

    return AVERROR(nvMapCreate(&map->map, mem, FFALIGN(size, 0x1000), 0x10000, NvKind_Pitch,
                               convert_cache_flags(flags)));;
#endif
}

int av_nvtegra_map_close(AVNVTegraMap *map) {
#ifndef __SWITCH__
    return av_nvtegra_map_free(map);
#else
    nvMapClose(&map->map);
    return 0;
#endif
}

int av_nvtegra_map_map(AVNVTegraMap *map) {
#ifndef __SWITCH__
    void *addr;

    addr = mmap(NULL, map->size, PROT_READ | PROT_WRITE, MAP_SHARED, map->handle, 0);
    if (addr == MAP_FAILED)
        return AVERROR(errno);

    map->cpu_addr = addr;

    return 0;
#else
    nvioctl_command_buffer_map params;
    int err;

    params = (nvioctl_command_buffer_map){
        .handle = map->map.handle,
    };

    err = nvioctlChannel_MapCommandBuffer(map->owner, &params, 1, false);
    if (R_FAILED(err))
        return AVERROR(err);

    map->iova = params.iova;

    return 0;
#endif
}

int av_nvtegra_map_unmap(AVNVTegraMap *map) {
    int err;
#ifndef __SWITCH__
    if (!map->cpu_addr)
        return 0;

    err = munmap(map->cpu_addr, map->size);
    if (err < 0)
        return AVERROR(errno);

    map->cpu_addr = NULL;

    return 0;
#else
    nvioctl_command_buffer_map params;

    if (!map->iova)
        return 0;

    params = (nvioctl_command_buffer_map){
        .handle = map->map.handle,
        .iova   = map->iova,
    };

    err = nvioctlChannel_UnmapCommandBuffer(map->owner, &params, 1, false);
    if (R_FAILED(err))
        return AVERROR(err);

    map->iova = 0;

    return 0;
#endif
}

int av_nvtegra_map_cache_op(AVNVTegraMap *map, int op, void *addr, size_t len) {
#ifndef __SWITCH__
    struct nvmap_cache_op args;

    args = (struct nvmap_cache_op){
        .addr   = (uintptr_t)addr,
        .len    = len,
        .handle = av_nvtegra_map_get_handle(map),
        .op     = op,
    };

    return AVERROR(ioctl(get_nvmap_fd(), NVMAP_IOC_CACHE, &args));
#else
    if (!map->map.is_cpu_cacheable)
        return 0;

    switch (op) {
        case NVMAP_CACHE_OP_WB:
            armDCacheClean(addr, len);
            break;
        default:
        case NVMAP_CACHE_OP_INV:
        case NVMAP_CACHE_OP_WB_INV:
            /* libnx internally performs a clean-invalidate, since invalidate is a privileged instruction */
            armDCacheFlush(addr, len);
            break;
    }

    return 0;
#endif
}

int av_nvtegra_map_realloc(AVNVTegraMap *map, uint32_t size, uint32_t align,
                           int heap_mask, int flags)
{
    AVNVTegraChannel channel;
    AVNVTegraMap tmp = {0};
    int err;

    if (av_nvtegra_map_get_size(map) >= size)
        return 0;

    /* Dummy channel object to hold the owner fd */
    channel = (AVNVTegraChannel){
#ifdef __SWITCH__
        .channel.fd = map->owner,
#endif
    };

    err = av_nvtegra_map_create(&tmp, &channel, size, align, heap_mask, flags);
    if (err < 0)
        goto fail;

    memcpy(av_nvtegra_map_get_addr(&tmp), av_nvtegra_map_get_addr(map), av_nvtegra_map_get_size(map));

    err = av_nvtegra_map_destroy(map);
    if (err < 0)
        goto fail;

    *map = tmp;

    return 0;

fail:
    av_nvtegra_map_destroy(&tmp);
    return err;
}

int av_nvtegra_cmdbuf_init(AVNVTegraCmdbuf *cmdbuf) {
    cmdbuf->num_cmdbufs      = 0;
#ifndef __SWITCH__
    cmdbuf->num_relocs       = 0;
    cmdbuf->num_waitchks     = 0;
#endif
    cmdbuf->num_syncpt_incrs = 0;

#define NUM_INITIAL_CMDBUFS      3
#define NUM_INITIAL_RELOCS       15
#define NUM_INITIAL_SYNCPT_INCRS 3

    cmdbuf->cmdbufs      = av_malloc_array(NUM_INITIAL_CMDBUFS, sizeof(*cmdbuf->cmdbufs));
#ifndef __SWITCH__
    cmdbuf->cmdbuf_exts  = av_malloc_array(NUM_INITIAL_CMDBUFS, sizeof(*cmdbuf->cmdbuf_exts));
    cmdbuf->class_ids    = av_malloc_array(NUM_INITIAL_CMDBUFS, sizeof(*cmdbuf->class_ids));
#endif

#ifndef __SWITCH__
    if (!cmdbuf->cmdbufs || !cmdbuf->cmdbuf_exts || !cmdbuf->class_ids)
#else
    if (!cmdbuf->cmdbufs)
#endif
        return AVERROR(ENOMEM);

#ifndef __SWITCH__
    cmdbuf->relocs       = av_malloc_array(NUM_INITIAL_RELOCS, sizeof(*cmdbuf->relocs));
    cmdbuf->reloc_types  = av_malloc_array(NUM_INITIAL_RELOCS, sizeof(*cmdbuf->reloc_types));
    cmdbuf->reloc_shifts = av_malloc_array(NUM_INITIAL_RELOCS, sizeof(*cmdbuf->reloc_shifts));
    if (!cmdbuf->relocs || !cmdbuf->reloc_types || !cmdbuf->reloc_shifts)
        return AVERROR(ENOMEM);
#endif

    cmdbuf->syncpt_incrs = av_malloc_array(NUM_INITIAL_SYNCPT_INCRS, sizeof(*cmdbuf->syncpt_incrs));
#ifndef __SWITCH__
    cmdbuf->fences       = av_malloc_array(NUM_INITIAL_SYNCPT_INCRS, sizeof(*cmdbuf->fences));
#endif

#ifndef __SWITCH__
    if (!cmdbuf->syncpt_incrs || !cmdbuf->fences)
#else
    if (!cmdbuf->syncpt_incrs)
#endif
        return AVERROR(ENOMEM);

    return 0;
}

int av_nvtegra_cmdbuf_deinit(AVNVTegraCmdbuf *cmdbuf) {
    av_freep(&cmdbuf->cmdbufs);
    av_freep(&cmdbuf->syncpt_incrs);

#ifndef __SWITCH__
    av_freep(&cmdbuf->cmdbuf_exts), av_freep(&cmdbuf->class_ids);
    av_freep(&cmdbuf->relocs), av_freep(&cmdbuf->reloc_types), av_freep(&cmdbuf->reloc_shifts);
    av_freep(&cmdbuf->fences);
#endif

    return 0;
}

int av_nvtegra_cmdbuf_add_memory(AVNVTegraCmdbuf *cmdbuf, AVNVTegraMap *map, uint32_t offset, uint32_t size) {
    uint8_t *mem;

    mem = av_nvtegra_map_get_addr(map);

    cmdbuf->map        = map;
    cmdbuf->mem_offset = offset;
    cmdbuf->mem_size   = size;

    cmdbuf->cur_word = (uint32_t *)(mem + cmdbuf->mem_offset);

    return 0;
}

int av_nvtegra_cmdbuf_clear(AVNVTegraCmdbuf *cmdbuf) {
    uint8_t *mem;

    mem = av_nvtegra_map_get_addr(cmdbuf->map);

    cmdbuf->num_cmdbufs = 0, cmdbuf->num_syncpt_incrs = 0;
#ifndef __SWITCH__
    cmdbuf->num_relocs = 0, cmdbuf->num_waitchks = 0;
#endif

    cmdbuf->cur_word = (uint32_t *)(mem + cmdbuf->mem_offset);
    return 0;
}

int av_nvtegra_cmdbuf_begin(AVNVTegraCmdbuf *cmdbuf, uint32_t class_id) {
    uint8_t *mem;
    void *tmp1;
#ifndef __SWITCH__
    void *tmp2, *tmp3;
#endif

    mem = av_nvtegra_map_get_addr(cmdbuf->map);

    tmp1 = av_realloc_array(cmdbuf->cmdbufs,     cmdbuf->num_cmdbufs + 1, sizeof(*cmdbuf->cmdbufs));
#ifndef __SWITCH__
    tmp2 = av_realloc_array(cmdbuf->cmdbuf_exts, cmdbuf->num_cmdbufs + 1, sizeof(*cmdbuf->cmdbuf_exts));
    tmp3 = av_realloc_array(cmdbuf->class_ids,   cmdbuf->num_cmdbufs + 1, sizeof(*cmdbuf->class_ids));
#endif

#ifndef __SWITCH__
    if (!tmp1 || !tmp2 || !tmp3)
#else
    if (!tmp1)
#endif
        return AVERROR(ENOMEM);

    cmdbuf->cmdbufs = tmp1;

#ifndef __SWITCH__
    cmdbuf->cmdbuf_exts = tmp2, cmdbuf->class_ids = tmp3;
#endif

    cmdbuf->cmdbufs[cmdbuf->num_cmdbufs] = (struct nvhost_cmdbuf){
        .mem       = av_nvtegra_map_get_handle(cmdbuf->map),
        .offset    = (uint8_t *)cmdbuf->cur_word - mem,
    };

#ifndef __SWITCH__
    cmdbuf->cmdbuf_exts[cmdbuf->num_cmdbufs] = (struct nvhost_cmdbuf_ext){
        .pre_fence = -1,
    };

    cmdbuf->class_ids[cmdbuf->num_cmdbufs] = class_id;
#endif

#ifdef __SWITCH__
    if (cmdbuf->num_cmdbufs == 0)
        av_nvtegra_cmdbuf_push_word(cmdbuf, host1x_opcode_setclass(class_id, 0, 0));
#endif

    return 0;
}

int av_nvtegra_cmdbuf_end(AVNVTegraCmdbuf *cmdbuf) {
    cmdbuf->num_cmdbufs++;
    return 0;
}

int av_nvtegra_cmdbuf_push_word(AVNVTegraCmdbuf *cmdbuf, uint32_t word) {
    uintptr_t mem_start = (uintptr_t)av_nvtegra_map_get_addr(cmdbuf->map) + cmdbuf->mem_offset;

    if ((uintptr_t)cmdbuf->cur_word - mem_start >= cmdbuf->mem_size)
        return AVERROR(ENOMEM);

    *cmdbuf->cur_word++ = word;
    cmdbuf->cmdbufs[cmdbuf->num_cmdbufs].words += 1;
    return 0;
}

int av_nvtegra_cmdbuf_push_value(AVNVTegraCmdbuf *cmdbuf, uint32_t offset, uint32_t word) {
    int err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf, host1x_opcode_incr(NV_THI_METHOD0>>2, 2));
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf, offset);
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf, word);
    if (err < 0)
        return err;

    return 0;
}

int av_nvtegra_cmdbuf_push_reloc(AVNVTegraCmdbuf *cmdbuf, uint32_t offset, AVNVTegraMap *target, uint32_t target_offset,
                                 int reloc_type, int shift)
{
    int err;
#ifndef __SWITCH__
    uint8_t *mem;
    void *tmp1, *tmp2, *tmp3;

    mem = av_nvtegra_map_get_addr(cmdbuf->map);

    tmp1 = av_realloc_array(cmdbuf->relocs,       cmdbuf->num_relocs + 1, sizeof(*cmdbuf->relocs));
    tmp2 = av_realloc_array(cmdbuf->reloc_types,  cmdbuf->num_relocs + 1, sizeof(*cmdbuf->reloc_types));
    tmp3 = av_realloc_array(cmdbuf->reloc_shifts, cmdbuf->num_relocs + 1, sizeof(*cmdbuf->reloc_shifts));
    if (!tmp1 || !tmp2 || !tmp3)
        return AVERROR(ENOMEM);

    cmdbuf->relocs = tmp1, cmdbuf->reloc_types = tmp2, cmdbuf->reloc_shifts = tmp3;

    err = av_nvtegra_cmdbuf_push_value(cmdbuf, offset, 0xdeadbeef);
    if (err < 0)
        return err;

    cmdbuf->relocs[cmdbuf->num_relocs]       = (struct nvhost_reloc){
        .cmdbuf_mem    = av_nvtegra_map_get_handle(cmdbuf->map),
        .cmdbuf_offset = (uint8_t *)cmdbuf->cur_word - mem - sizeof(uint32_t),
        .target        = av_nvtegra_map_get_handle(target),
        .target_offset = target_offset,
    };

    cmdbuf->reloc_types[cmdbuf->num_relocs]  = (struct nvhost_reloc_type){
        .reloc_type    = reloc_type,
    };

    cmdbuf->reloc_shifts[cmdbuf->num_relocs] = (struct nvhost_reloc_shift){
        .shift         = shift,
    };

    cmdbuf->num_relocs++;

    return 0;
#else
    err = av_nvtegra_cmdbuf_push_value(cmdbuf, offset, (target->iova + target_offset) >> shift);
    if (err < 0)
        return err;

    return 0;
#endif
}

int av_nvtegra_cmdbuf_push_syncpt_incr(AVNVTegraCmdbuf *cmdbuf, uint32_t syncpt) {
    int err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf, host1x_opcode_nonincr(NV_THI_INCR_SYNCPT>>2, 1));
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf,
                                      AV_NVTEGRA_VALUE(NV_THI_INCR_SYNCPT, INDX, syncpt) |
                                      AV_NVTEGRA_ENUM (NV_THI_INCR_SYNCPT, COND, OP_DONE));
    if (err < 0)
        return err;

    return 0;
}

int av_nvtegra_cmdbuf_push_wait(AVNVTegraCmdbuf *cmdbuf, uint32_t syncpt, uint32_t fence) {
    int err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf, host1x_opcode_setclass(HOST1X_CLASS_HOST1X, 0, 0));
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf, host1x_opcode_mask(NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD>>2,
                                      (1<<(NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD - NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD)) |
                                      (1<<(NV_CLASS_HOST_WAIT_SYNCPT         - NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD))));
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf, fence);
    if (err < 0)
        return err;

    err = av_nvtegra_cmdbuf_push_word(cmdbuf, syncpt);
    if (err < 0)
        return err;

    return 0;
}

int av_nvtegra_cmdbuf_add_syncpt_incr(AVNVTegraCmdbuf *cmdbuf, uint32_t syncpt, uint32_t fence)
{
    void *tmp1;
#ifndef __SWITCH__
    void *tmp2;
#endif

    tmp1 = av_realloc_array(cmdbuf->syncpt_incrs, cmdbuf->num_syncpt_incrs + 1, sizeof(*cmdbuf->syncpt_incrs));
#ifndef __SWITCH__
    tmp2 = av_realloc_array(cmdbuf->fences,       cmdbuf->num_syncpt_incrs + 1, sizeof(*cmdbuf->fences));
#endif

#ifndef __SWITCH__
    if (!tmp1 || !tmp2)
#else
    if (!tmp1)
#endif
        return AVERROR(ENOMEM);

    cmdbuf->syncpt_incrs = tmp1;
#ifndef __SWITCH__
    cmdbuf->fences       = tmp2;
#endif

    cmdbuf->syncpt_incrs[cmdbuf->num_syncpt_incrs] = (struct nvhost_syncpt_incr){
        .syncpt_id    = syncpt,
        .syncpt_incrs = 1,
    };

#ifndef __SWITCH__
    cmdbuf->fences[cmdbuf->num_syncpt_incrs]       = fence;
#endif

    cmdbuf->num_syncpt_incrs++;

    return av_nvtegra_cmdbuf_push_syncpt_incr(cmdbuf, syncpt);
}

int av_nvtegra_cmdbuf_add_waitchk(AVNVTegraCmdbuf *cmdbuf, uint32_t syncpt, uint32_t fence) {
#ifndef __SWITCH__
    uint8_t *mem;
    void *tmp;

    mem = av_nvtegra_map_get_addr(cmdbuf->map);

    tmp = av_realloc_array(cmdbuf->waitchks, cmdbuf->num_waitchks + 1, sizeof(*cmdbuf->waitchks));
    if (!tmp)
        return AVERROR(ENOMEM);

    cmdbuf->waitchks = tmp;

    cmdbuf->waitchks[cmdbuf->num_waitchks] = (struct nvhost_waitchk){
        .mem       = av_nvtegra_map_get_handle(cmdbuf->map),
        .offset    = (uint8_t *)cmdbuf->cur_word - mem - sizeof(uint32_t),
        .syncpt_id = syncpt,
        .thresh    = fence,
    };

    cmdbuf->num_waitchks++;
#endif

    return av_nvtegra_cmdbuf_push_wait(cmdbuf, syncpt, fence);
}

static void nvtegra_job_free(void *opaque, uint8_t *data) {
    AVNVTegraJob *job = (AVNVTegraJob *)data;

    if (!job)
        return;

    av_nvtegra_cmdbuf_deinit(&job->cmdbuf);
    av_nvtegra_map_destroy(&job->input_map);

    av_freep(&job);
}

static AVBufferRef *nvtegra_job_alloc(void *opaque, size_t size) {
    AVNVTegraJobPool *pool = opaque;

    AVBufferRef  *buffer;
    AVNVTegraJob *job;
    int err;

    job = av_mallocz(sizeof(*job));
    if (!job)
        return NULL;

    err = av_nvtegra_map_create(&job->input_map, pool->channel, pool->input_map_size, 0x100,
                                NVMAP_HEAP_IOVMM, NVMAP_HANDLE_WRITE_COMBINE);
    if (err < 0)
        goto fail;

    err = av_nvtegra_cmdbuf_init(&job->cmdbuf);
    if (err < 0)
        goto fail;

    err = av_nvtegra_cmdbuf_add_memory(&job->cmdbuf, &job->input_map, pool->cmdbuf_off, pool->max_cmdbuf_size);
    if (err < 0)
        goto fail;

    buffer = av_buffer_create((uint8_t *)job, sizeof(*job), nvtegra_job_free, pool, 0);
    if (!buffer)
        goto fail;

    return buffer;

fail:
    av_nvtegra_cmdbuf_deinit(&job->cmdbuf);
    av_nvtegra_map_destroy(&job->input_map);
    av_freep(job);
    return NULL;
}

int av_nvtegra_job_pool_init(AVNVTegraJobPool *pool, AVNVTegraChannel *channel,
                             size_t input_map_size, off_t cmdbuf_off, size_t max_cmdbuf_size)
{
    pool->channel         = channel;
    pool->input_map_size  = input_map_size;
    pool->cmdbuf_off      = cmdbuf_off;
    pool->max_cmdbuf_size = max_cmdbuf_size;
    pool->pool            = av_buffer_pool_init2(sizeof(AVNVTegraJob), pool,
                                                 nvtegra_job_alloc, NULL);
    if (!pool->pool)
        return AVERROR(ENOMEM);

    return 0;
}

int av_nvtegra_job_pool_uninit(AVNVTegraJobPool *pool) {
    av_buffer_pool_uninit(&pool->pool);
    return 0;
}

AVBufferRef *av_nvtegra_job_pool_get(AVNVTegraJobPool *pool) {
    return av_buffer_pool_get(pool->pool);
}

int av_nvtegra_job_submit(AVNVTegraJobPool *pool, AVNVTegraJob *job) {
    return av_nvtegra_channel_submit(pool->channel, &job->cmdbuf, &job->fence);
}

int av_nvtegra_job_wait(AVNVTegraJobPool *pool, AVNVTegraJob *job, int timeout) {
    return av_nvtegra_syncpt_wait(pool->channel, job->fence, timeout);
}
