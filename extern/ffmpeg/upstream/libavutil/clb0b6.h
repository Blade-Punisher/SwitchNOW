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

#ifndef _clb0b6_h_
#define _clb0b6_h_

#ifdef __cplusplus
extern "C" {
#endif

#define NVB0B6_VIDEO_COMPOSITOR                                                 (0x0000B0B6)

#define NVB0B6_VIDEO_COMPOSITOR_NOP                                             (0x00000100)
#define NVB0B6_VIDEO_COMPOSITOR_NOP_PARAMETER                                   31:0
#define NVB0B6_VIDEO_COMPOSITOR_PM_TRIGGER                                      (0x00000140)
#define NVB0B6_VIDEO_COMPOSITOR_PM_TRIGGER_V                                    31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_APPLICATION_ID                              (0x00000200)
#define NVB0B6_VIDEO_COMPOSITOR_SET_APPLICATION_ID_ID                           31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_APPLICATION_ID_ID_COMPOSITOR                (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_SET_WATCHDOG_TIMER                              (0x00000204)
#define NVB0B6_VIDEO_COMPOSITOR_SET_WATCHDOG_TIMER_TIMER                        31:0
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_A                                     (0x00000240)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_A_UPPER                               7:0
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_B                                     (0x00000244)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_B_LOWER                               31:0
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_C                                     (0x00000248)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_C_PAYLOAD                             31:0
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SAVE_AREA                                   (0x0000024C)
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SAVE_AREA_OFFSET                            27:0
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SAVE_AREA_CTX_VALID                         31:28
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH                                      (0x00000250)
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH_RESTORE                              0:0
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH_RESTORE_FALSE                        (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH_RESTORE_TRUE                         (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH_RST_NOTIFY                           1:1
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH_RST_NOTIFY_FALSE                     (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH_RST_NOTIFY_TRUE                      (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH_RESERVED                             7:2
#define NVB0B6_VIDEO_COMPOSITOR_CTX_SWITCH_ASID                                 23:8
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE                                         (0x00000300)
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_NOTIFY                                  0:0
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_NOTIFY_DISABLE                          (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_NOTIFY_ENABLE                           (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_NOTIFY_ON                               1:1
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_NOTIFY_ON_END                           (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_NOTIFY_ON_BEGIN                         (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_AWAKEN                                  8:8
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_AWAKEN_DISABLE                          (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_EXECUTE_AWAKEN_ENABLE                           (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D                                     (0x00000304)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_STRUCTURE_SIZE                      0:0
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_STRUCTURE_SIZE_ONE                  (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_STRUCTURE_SIZE_FOUR                 (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_AWAKEN_ENABLE                       8:8
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_AWAKEN_ENABLE_FALSE                 (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_AWAKEN_ENABLE_TRUE                  (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_OPERATION                           17:16
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_OPERATION_RELEASE                   (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_OPERATION_RESERVED0                 (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_OPERATION_RESERVED1                 (0x00000002)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_OPERATION_TRAP                      (0x00000003)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_FLUSH_DISABLE                       21:21
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_FLUSH_DISABLE_FALSE                 (0x00000000)
#define NVB0B6_VIDEO_COMPOSITOR_SEMAPHORE_D_FLUSH_DISABLE_TRUE                  (0x00000001)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_LUMA_OFFSET(b)                     (0x00000400 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_LUMA_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_CHROMA_U_OFFSET(b)                 (0x00000404 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_CHROMA_U_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_CHROMA_V_OFFSET(b)                 (0x00000408 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE0_CHROMA_V_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE1_LUMA_OFFSET(b)                     (0x0000040C + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE1_LUMA_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE1_CHROMA_U_OFFSET(b)                 (0x00000410 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE1_CHROMA_U_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE1_CHROMA_V_OFFSET(b)                 (0x00000414 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE1_CHROMA_V_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE2_LUMA_OFFSET(b)                     (0x00000418 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE2_LUMA_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE2_CHROMA_U_OFFSET(b)                 (0x0000041C + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE2_CHROMA_U_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE2_CHROMA_V_OFFSET(b)                 (0x00000420 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE2_CHROMA_V_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE3_LUMA_OFFSET(b)                     (0x00000424 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE3_LUMA_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE3_CHROMA_U_OFFSET(b)                 (0x00000428 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE3_CHROMA_U_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE3_CHROMA_V_OFFSET(b)                 (0x0000042C + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE3_CHROMA_V_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE4_LUMA_OFFSET(b)                     (0x00000430 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE4_LUMA_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE4_CHROMA_U_OFFSET(b)                 (0x00000434 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE4_CHROMA_U_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE4_CHROMA_V_OFFSET(b)                 (0x00000438 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE4_CHROMA_V_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE5_LUMA_OFFSET(b)                     (0x0000043C + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE5_LUMA_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE5_CHROMA_U_OFFSET(b)                 (0x00000440 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE5_CHROMA_U_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE5_CHROMA_V_OFFSET(b)                 (0x00000444 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE5_CHROMA_V_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE6_LUMA_OFFSET(b)                     (0x00000448 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE6_LUMA_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE6_CHROMA_U_OFFSET(b)                 (0x0000044C + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE6_CHROMA_U_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE6_CHROMA_V_OFFSET(b)                 (0x00000450 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE6_CHROMA_V_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE7_LUMA_OFFSET(b)                     (0x00000454 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE7_LUMA_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE7_CHROMA_U_OFFSET(b)                 (0x00000458 + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE7_CHROMA_U_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE7_CHROMA_V_OFFSET(b)                 (0x0000045C + (b)*0x00000060)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SURFACE7_CHROMA_V_OFFSET_OFFSET             31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_PICTURE_INDEX                               (0x00000700)
#define NVB0B6_VIDEO_COMPOSITOR_SET_PICTURE_INDEX_INDEX                         31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS                              (0x00000704)
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS_GPTIMER_ON                   0:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS_DEBUG_MODE                   4:4
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS_FALCON_CONTROL               8:8
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTROL_PARAMS_CONFIG_STRUCT_SIZE           31:16
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONFIG_STRUCT_OFFSET                        (0x00000708)
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONFIG_STRUCT_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_FILTER_STRUCT_OFFSET                        (0x0000070C)
#define NVB0B6_VIDEO_COMPOSITOR_SET_FILTER_STRUCT_OFFSET_OFFSET                 31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_PALETTE_OFFSET                              (0x00000710)
#define NVB0B6_VIDEO_COMPOSITOR_SET_PALETTE_OFFSET_OFFSET                       31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_HIST_OFFSET                                 (0x00000714)
#define NVB0B6_VIDEO_COMPOSITOR_SET_HIST_OFFSET_OFFSET                          31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTEXT_ID                                  (0x00000718)
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTEXT_ID_FCE_UCODE                        3:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTEXT_ID_CONFIG                           7:4
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTEXT_ID_PALETTE                          11:8
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTEXT_ID_OUTPUT                           15:12
#define NVB0B6_VIDEO_COMPOSITOR_SET_CONTEXT_ID_HIST                             19:16
#define NVB0B6_VIDEO_COMPOSITOR_SET_FCE_UCODE_SIZE                              (0x0000071C)
#define NVB0B6_VIDEO_COMPOSITOR_SET_FCE_UCODE_SIZE_FCE_SZ                       15:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_LUMA_OFFSET                  (0x00000720)
#define NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_LUMA_OFFSET_OFFSET           31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_CHROMA_U_OFFSET              (0x00000724)
#define NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_CHROMA_U_OFFSET_OFFSET       31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_CHROMA_V_OFFSET              (0x00000728)
#define NVB0B6_VIDEO_COMPOSITOR_SET_OUTPUT_SURFACE_CHROMA_V_OFFSET_OFFSET       31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_FCE_UCODE_OFFSET                            (0x0000072C)
#define NVB0B6_VIDEO_COMPOSITOR_SET_FCE_UCODE_OFFSET_OFFSET                     31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_CRC_STRUCT_OFFSET                           (0x00000730)
#define NVB0B6_VIDEO_COMPOSITOR_SET_CRC_STRUCT_OFFSET_OFFSET                    31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_CRC_MODE                                    (0x00000734)
#define NVB0B6_VIDEO_COMPOSITOR_SET_CRC_MODE_INTF_PART_ASEL                     3:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_CRC_MODE_INTF_PART_BSEL                     7:4
#define NVB0B6_VIDEO_COMPOSITOR_SET_CRC_MODE_INTF_PART_CSEL                     11:8
#define NVB0B6_VIDEO_COMPOSITOR_SET_CRC_MODE_INTF_PART_DSEL                     15:12
#define NVB0B6_VIDEO_COMPOSITOR_SET_CRC_MODE_CRC_MODE                           16:16
#define NVB0B6_VIDEO_COMPOSITOR_SET_STATUS_OFFSET                               (0x00000738)
#define NVB0B6_VIDEO_COMPOSITOR_SET_STATUS_OFFSET_OFFSET                        31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID(b)                          (0x00000740 + (b)*0x00000004)
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID_CTX_ID_SFC0                 3:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID_CTX_ID_SFC1                 7:4
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID_CTX_ID_SFC2                 11:8
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID_CTX_ID_SFC3                 15:12
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID_CTX_ID_SFC4                 19:16
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID_CTX_ID_SFC5                 23:20
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID_CTX_ID_SFC6                 27:24
#define NVB0B6_VIDEO_COMPOSITOR_SET_SLOT_CONTEXT_ID_CTX_ID_SFC7                 31:28
#define NVB0B6_VIDEO_COMPOSITOR_SET_HISTORY_BUFFER_OFFSET(b)                    (0x00000780 + (b)*0x00000004)
#define NVB0B6_VIDEO_COMPOSITOR_SET_HISTORY_BUFFER_OFFSET_OFFSET                31:0
#define NVB0B6_VIDEO_COMPOSITOR_SET_COMP_TAG_BUFFER_OFFSET(b)                   (0x000007C0 + (b)*0x00000004)
#define NVB0B6_VIDEO_COMPOSITOR_SET_COMP_TAG_BUFFER_OFFSET_OFFSET               31:0
#define NVB0B6_VIDEO_COMPOSITOR_PM_TRIGGER_END                                  (0x00001114)
#define NVB0B6_VIDEO_COMPOSITOR_PM_TRIGGER_END_V                                31:0

#define NVB0B6_DXVAHD_FRAME_FORMAT_PROGRESSIVE                                  0
#define NVB0B6_DXVAHD_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST                   1
#define NVB0B6_DXVAHD_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST                2
#define NVB0B6_DXVAHD_FRAME_FORMAT_TOP_FIELD                                    3
#define NVB0B6_DXVAHD_FRAME_FORMAT_BOTTOM_FIELD                                 4
#define NVB0B6_DXVAHD_FRAME_FORMAT_SUBPIC_PROGRESSIVE                           5
#define NVB0B6_DXVAHD_FRAME_FORMAT_SUBPIC_INTERLACED_TOP_FIELD_FIRST            6
#define NVB0B6_DXVAHD_FRAME_FORMAT_SUBPIC_INTERLACED_BOTTOM_FIELD_FIRST         7
#define NVB0B6_DXVAHD_FRAME_FORMAT_SUBPIC_TOP_FIELD                             8
#define NVB0B6_DXVAHD_FRAME_FORMAT_SUBPIC_BOTTOM_FIELD                          9
#define NVB0B6_DXVAHD_FRAME_FORMAT_TOP_FIELD_CHROMA_BOTTOM                      10
#define NVB0B6_DXVAHD_FRAME_FORMAT_BOTTOM_FIELD_CHROMA_TOP                      11
#define NVB0B6_DXVAHD_FRAME_FORMAT_SUBPIC_TOP_FIELD_CHROMA_BOTTOM               12
#define NVB0B6_DXVAHD_FRAME_FORMAT_SUBPIC_BOTTOM_FIELD_CHROMA_TOP               13

#define NVB0B6_T_A8                                                             0
#define NVB0B6_T_L8                                                             1
#define NVB0B6_T_A4L4                                                           2
#define NVB0B6_T_L4A4                                                           3
#define NVB0B6_T_R8                                                             4
#define NVB0B6_T_A8L8                                                           5
#define NVB0B6_T_L8A8                                                           6
#define NVB0B6_T_R8G8                                                           7
#define NVB0B6_T_G8R8                                                           8
#define NVB0B6_T_B5G6R5                                                         9
#define NVB0B6_T_R5G6B5                                                         10
#define NVB0B6_T_B6G5R5                                                         11
#define NVB0B6_T_R5G5B6                                                         12
#define NVB0B6_T_A1B5G5R5                                                       13
#define NVB0B6_T_A1R5G5B5                                                       14
#define NVB0B6_T_B5G5R5A1                                                       15
#define NVB0B6_T_R5G5B5A1                                                       16
#define NVB0B6_T_A5B5G5R1                                                       17
#define NVB0B6_T_A5R1G5B5                                                       18
#define NVB0B6_T_B5G5R1A5                                                       19
#define NVB0B6_T_R1G5B5A5                                                       20
#define NVB0B6_T_X1B5G5R5                                                       21
#define NVB0B6_T_X1R5G5B5                                                       22
#define NVB0B6_T_B5G5R5X1                                                       23
#define NVB0B6_T_R5G5B5X1                                                       24
#define NVB0B6_T_A4B4G4R4                                                       25
#define NVB0B6_T_A4R4G4B4                                                       26
#define NVB0B6_T_B4G4R4A4                                                       27
#define NVB0B6_T_R4G4B4A4                                                       28
#define NVB0B6_T_B8_G8_R8                                                       29
#define NVB0B6_T_R8_G8_B8                                                       30
#define NVB0B6_T_A8B8G8R8                                                       31
#define NVB0B6_T_A8R8G8B8                                                       32
#define NVB0B6_T_B8G8R8A8                                                       33
#define NVB0B6_T_R8G8B8A8                                                       34
#define NVB0B6_T_X8B8G8R8                                                       35
#define NVB0B6_T_X8R8G8B8                                                       36
#define NVB0B6_T_B8G8R8X8                                                       37
#define NVB0B6_T_R8G8B8X8                                                       38
#define NVB0B6_T_A2B10G10R10                                                    39
#define NVB0B6_T_A2R10G10B10                                                    40
#define NVB0B6_T_B10G10R10A2                                                    41
#define NVB0B6_T_R10G10B10A2                                                    42
#define NVB0B6_T_A4P4                                                           43
#define NVB0B6_T_P4A4                                                           44
#define NVB0B6_T_P8A8                                                           45
#define NVB0B6_T_A8P8                                                           46
#define NVB0B6_T_P8                                                             47
#define NVB0B6_T_P1                                                             48
#define NVB0B6_T_U8V8                                                           49
#define NVB0B6_T_V8U8                                                           50
#define NVB0B6_T_A8Y8U8V8                                                       51
#define NVB0B6_T_V8U8Y8A8                                                       52
#define NVB0B6_T_Y8_U8_V8                                                       53
#define NVB0B6_T_Y8_V8_U8                                                       54
#define NVB0B6_T_U8_V8_Y8                                                       55
#define NVB0B6_T_V8_U8_Y8                                                       56
#define NVB0B6_T_Y8_U8__Y8_V8                                                   57
#define NVB0B6_T_Y8_V8__Y8_U8                                                   58
#define NVB0B6_T_U8_Y8__V8_Y8                                                   59
#define NVB0B6_T_V8_Y8__U8_Y8                                                   60
#define NVB0B6_T_Y8___U8V8_N444                                                 61
#define NVB0B6_T_Y8___V8U8_N444                                                 62
#define NVB0B6_T_Y8___U8V8_N422                                                 63
#define NVB0B6_T_Y8___V8U8_N422                                                 64
#define NVB0B6_T_Y8___U8V8_N422R                                                65
#define NVB0B6_T_Y8___V8U8_N422R                                                66
#define NVB0B6_T_Y8___U8V8_N420                                                 67
#define NVB0B6_T_Y8___V8U8_N420                                                 68
#define NVB0B6_T_Y8___U8___V8_N444                                              69
#define NVB0B6_T_Y8___U8___V8_N422                                              70
#define NVB0B6_T_Y8___U8___V8_N422R                                             71
#define NVB0B6_T_Y8___U8___V8_N420                                              72
#define NVB0B6_T_U8                                                             73
#define NVB0B6_T_V8                                                             74

#define NVB0B6_DXVAHD_ALPHA_FILL_MODE_OPAQUE                                    0
#define NVB0B6_DXVAHD_ALPHA_FILL_MODE_BACKGROUND                                1
#define NVB0B6_DXVAHD_ALPHA_FILL_MODE_DESTINATION                               2
#define NVB0B6_DXVAHD_ALPHA_FILL_MODE_SOURCE_STREAM                             3
#define NVB0B6_DXVAHD_ALPHA_FILL_MODE_COMPOSITED                                4
#define NVB0B6_DXVAHD_ALPHA_FILL_MODE_SOURCE_ALPHA                              5

#define NVB0B6_BLK_KIND_PITCH                                                   0
#define NVB0B6_BLK_KIND_GENERIC_16Bx2                                           1
#define NVB0B6_BLK_KIND_BL_NAIVE                                                2
#define NVB0B6_BLK_KIND_BL_KEPLER_XBAR_RAW                                      3
#define NVB0B6_BLK_KIND_VP2_TILED                                               15

#define NVB0B6_FILTER_LENGTH_1TAP                                               0
#define NVB0B6_FILTER_LENGTH_2TAP                                               1
#define NVB0B6_FILTER_LENGTH_5TAP                                               2
#define NVB0B6_FILTER_LENGTH_10TAP                                              3

#define NVB0B6_FILTER_TYPE_NORMAL                                               0
#define NVB0B6_FILTER_TYPE_NOISE                                                1
#define NVB0B6_FILTER_TYPE_DETAIL                                               2

#ifdef __cplusplus
};     /* extern "C" */
#endif
#endif // _clb0b6_h
