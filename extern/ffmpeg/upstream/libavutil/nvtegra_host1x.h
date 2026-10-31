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

#ifndef AVUTIL_NVTEGRA_HOST1X_H
#define AVUTIL_NVTEGRA_HOST1X_H

#include <stdint.h>

#include "macros.h"

/* From L4T include/linux/host1x.h */
enum host1x_class {
    HOST1X_CLASS_HOST1X  = 0x01,
    HOST1X_CLASS_NVENC   = 0x21,
    HOST1X_CLASS_VI      = 0x30,
    HOST1X_CLASS_ISPA    = 0x32,
    HOST1X_CLASS_ISPB    = 0x34,
    HOST1X_CLASS_GR2D    = 0x51,
    HOST1X_CLASS_GR2D_SB = 0x52,
    HOST1X_CLASS_VIC     = 0x5d,
    HOST1X_CLASS_GR3D    = 0x60,
    HOST1X_CLASS_NVJPG   = 0xc0,
    HOST1X_CLASS_NVDEC   = 0xf0,
};

static inline uint32_t host1x_opcode_setclass(unsigned class_id, unsigned offset, unsigned mask) {
    return (0 << 28) | (offset << 16) | (class_id << 6) | mask;
}

static inline uint32_t host1x_opcode_incr(unsigned offset, unsigned count) {
    return (1 << 28) | (offset << 16) | count;
}

static inline uint32_t host1x_opcode_nonincr(unsigned offset, unsigned count) {
    return (2 << 28) | (offset << 16) | count;
}

static inline uint32_t host1x_opcode_mask(unsigned offset, unsigned mask) {
    return (3 << 28) | (offset << 16) | mask;
}

static inline uint32_t host1x_opcode_imm(unsigned offset, unsigned value) {
    return (4 << 28) | (offset << 16) | value;
}

#define NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD                                  (0x00000138)
#define NV_CLASS_HOST_WAIT_SYNCPT                                          (0x00000140)

#define NV_THI_INCR_SYNCPT                                                 (0x00000000)
#define NV_THI_INCR_SYNCPT_INDX                                            7:0
#define NV_THI_INCR_SYNCPT_COND                                            15:8
#define NV_THI_INCR_SYNCPT_COND_IMMEDIATE                                  (0x00000000)
#define NV_THI_INCR_SYNCPT_COND_OP_DONE                                    (0x00000001)
#define NV_THI_INCR_SYNCPT_ERR                                             (0x00000008)
#define NV_THI_INCR_SYNCPT_ERR_COND_STS_IMM                                0:0
#define NV_THI_INCR_SYNCPT_ERR_COND_STS_OPDONE                             1:1
#define NV_THI_CTXSW_INCR_SYNCPT                                           (0x0000000c)
#define NV_THI_CTXSW_INCR_SYNCPT_INDX                                      7:0
#define NV_THI_CTXSW                                                       (0x00000020)
#define NV_THI_CTXSW_CURR_CLASS                                            9:0
#define NV_THI_CTXSW_AUTO_ACK                                              11:11
#define NV_THI_CTXSW_CURR_CHANNEL                                          15:12
#define NV_THI_CTXSW_NEXT_CLASS                                            25:16
#define NV_THI_CTXSW_NEXT_CHANNEL                                          31:28
#define NV_THI_CONT_SYNCPT_EOF                                             (0x00000028)
#define NV_THI_CONT_SYNCPT_EOF_INDEX                                       7:0
#define NV_THI_CONT_SYNCPT_EOF_COND                                        8:8
#define NV_THI_METHOD0                                                     (0x00000040)
#define NV_THI_METHOD0_OFFSET                                              11:0
#define NV_THI_METHOD1                                                     (0x00000044)
#define NV_THI_METHOD1_DATA                                                31:0
#define NV_THI_INT_STATUS                                                  (0x00000078)
#define NV_THI_INT_STATUS_FALCON_INT                                       0:0
#define NV_THI_INT_MASK                                                    (0x0000007c)
#define NV_THI_INT_MASK_FALCON_INT                                         0:0

#endif /* AVUTIL_NVTEGRA_HOST1X_H */
