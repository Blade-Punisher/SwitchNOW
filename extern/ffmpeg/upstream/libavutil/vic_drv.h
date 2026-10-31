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

#ifndef __VIC_DRV_H
#define __VIC_DRV_H

#include <stdint.h>

typedef uint8_t  NvU8;
typedef uint16_t NvU16;
typedef uint32_t NvU32;
typedef uint64_t NvU64;
typedef  int8_t  NvS8;
typedef  int16_t NvS16;
typedef  int32_t NvS32;
typedef  int64_t NvS64;
typedef _Bool    NvBool;

typedef struct VicPipeConfig {
    NvU32 DownsampleHoriz       : 11;
    NvU32 reserved0             :  5;
    NvU32 DownsampleVert        : 11;
    NvU32 reserved1             :  5;
    NvU32 reserved2             : 32;
    NvU32 reserved3             : 32;
    NvU32 reserved4             : 32;
} VicPipeConfig;

typedef struct VicOutputConfig {
    NvU64 AlphaFillMode         :  3;
    NvU64 AlphaFillSlot         :  3;
    NvU64 BackgroundAlpha       : 10;
    NvU64 BackgroundR           : 10;
    NvU64 BackgroundG           : 10;
    NvU64 BackgroundB           : 10;
    NvU64 RegammaMode           :  2;
    NvU64 OutputFlipX           :  1;
    NvU64 OutputFlipY           :  1;
    NvU64 OutputTranspose       :  1;
    NvU64 reserved1             :  1;
    NvU64 reserved2             : 12;
    NvU32 TargetRectLeft        : 14;
    NvU32 reserved3             :  2;
    NvU32 TargetRectRight       : 14;
    NvU32 reserved4             :  2;
    NvU32 TargetRectTop         : 14;
    NvU32 reserved5             :  2;
    NvU32 TargetRectBottom      : 14;
    NvU32 reserved6             :  2;
} VicOutputConfig;

typedef struct VicOutputSurfaceConfig {
    NvU32 OutPixelFormat        :  7;
    NvU32 OutChromaLocHoriz     :  2;
    NvU32 OutChromaLocVert      :  2;
    NvU32 OutBlkKind            :  4;
    NvU32 OutBlkHeight          :  4;
    NvU32 reserved0             :  3;
    NvU32 reserved1             : 10;
    NvU32 OutSurfaceWidth       : 14;
    NvU32 OutSurfaceHeight      : 14;
    NvU32 reserved2             :  4;
    NvU32 OutLumaWidth          : 14;
    NvU32 OutLumaHeight         : 14;
    NvU32 reserved3             :  4;
    NvU32 OutChromaWidth        : 14;
    NvU32 OutChromaHeight       : 14;
    NvU32 reserved4             :  4;
} VicOutputSurfaceConfig;

typedef struct VicMatrixStruct {
    NvU64 matrix_coeff00        : 20;
    NvU64 matrix_coeff10        : 20;
    NvU64 matrix_coeff20        : 20;
    NvU64 matrix_r_shift        :  4;
    NvU64 matrix_coeff01        : 20;
    NvU64 matrix_coeff11        : 20;
    NvU64 matrix_coeff21        : 20;
    NvU64 reserved0             :  3;
    NvU64 matrix_enable         :  1;
    NvU64 matrix_coeff02        : 20;
    NvU64 matrix_coeff12        : 20;
    NvU64 matrix_coeff22        : 20;
    NvU64 reserved1             :  4;
    NvU64 matrix_coeff03        : 20;
    NvU64 matrix_coeff13        : 20;
    NvU64 matrix_coeff23        : 20;
    NvU64 reserved2             :  4;
} VicMatrixStruct;

typedef struct VicClearRectStruct {
    NvU32 ClearRect0Left        : 14;
    NvU32 reserved0             :  2;
    NvU32 ClearRect0Right       : 14;
    NvU32 reserved1             :  2;
    NvU32 ClearRect0Top         : 14;
    NvU32 reserved2             :  2;
    NvU32 ClearRect0Bottom      : 14;
    NvU32 reserved3             :  2;
    NvU32 ClearRect1Left        : 14;
    NvU32 reserved4             :  2;
    NvU32 ClearRect1Right       : 14;
    NvU32 reserved5             :  2;
    NvU32 ClearRect1Top         : 14;
    NvU32 reserved6             :  2;
    NvU32 ClearRect1Bottom      : 14;
    NvU32 reserved7             :  2;
} VicClearRectStruct;

typedef struct VicSlotStructSlotConfig {
    NvU64 SlotEnable            :  1;
    NvU64 DeNoise               :  1;
    NvU64 AdvancedDenoise       :  1;
    NvU64 CadenceDetect         :  1;
    NvU64 MotionMap             :  1;
    NvU64 MMapCombine           :  1;
    NvU64 IsEven                :  1;
    NvU64 ChromaEven            :  1;
    NvU64 CurrentFieldEnable    :  1;
    NvU64 PrevFieldEnable       :  1;
    NvU64 NextFieldEnable       :  1;
    NvU64 NextNrFieldEnable     :  1;
    NvU64 CurMotionFieldEnable  :  1;
    NvU64 PrevMotionFieldEnable :  1;
    NvU64 PpMotionFieldEnable   :  1;
    NvU64 CombMotionFieldEnable :  1;
    NvU64 FrameFormat           :  4;
    NvU64 FilterLengthY         :  2;
    NvU64 FilterLengthX         :  2;
    NvU64 Panoramic             : 12;
    NvU64 reserved1             : 22;
    NvU64 DetailFltClamp        :  6;
    NvU64 FilterNoise           : 10;
    NvU64 FilterDetail          : 10;
    NvU64 ChromaNoise           : 10;
    NvU64 ChromaDetail          : 10;
    NvU64 DeinterlaceMode       :  4;
    NvU64 MotionAccumWeight     :  3;
    NvU64 NoiseIir              : 11;
    NvU64 LightLevel            :  4;
    NvU64 reserved4             :  2;
    NvU32 SoftClampLow          : 10;
    NvU32 SoftClampHigh         : 10;
    NvU32 reserved5             :  3;
    NvU32 reserved6             :  9;
    NvU32 PlanarAlpha           : 10;
    NvU32 ConstantAlpha         :  1;
    NvU32 StereoInterleave      :  3;
    NvU32 ClipEnabled           :  1;
    NvU32 ClearRectMask         :  8;
    NvU32 DegammaMode           :  2;
    NvU32 reserved7             :  1;
    NvU32 DecompressEnable      :  1;
    NvU32 reserved9             :  5;
    NvU64 DecompressCtbCount    :  8;
    NvU64 DecompressZbcColor    : 32;
    NvU64 reserved12            : 24;
    NvU32 SourceRectLeft        : 30;
    NvU32 reserved14            :  2;
    NvU32 SourceRectRight       : 30;
    NvU32 reserved15            :  2;
    NvU32 SourceRectTop         : 30;
    NvU32 reserved16            :  2;
    NvU32 SourceRectBottom      : 30;
    NvU32 reserved17            :  2;
    NvU32 DestRectLeft          : 14;
    NvU32 reserved18            :  2;
    NvU32 DestRectRight         : 14;
    NvU32 reserved19            :  2;
    NvU32 DestRectTop           : 14;
    NvU32 reserved20            :  2;
    NvU32 DestRectBottom        : 14;
    NvU32 reserved21            :  2;
    NvU32 reserved22            : 32;
    NvU32 reserved23            : 32;
} VicSlotStructSlotConfig;

typedef struct VicSlotStructSlotSurfaceConfig {
    NvU32 SlotPixelFormat       :  7;
    NvU32 SlotChromaLocHoriz    :  2;
    NvU32 SlotChromaLocVert     :  2;
    NvU32 SlotBlkKind           :  4;
    NvU32 SlotBlkHeight         :  4;
    NvU32 SlotCacheWidth        :  3;
    NvU32 reserved0             : 10;
    NvU32 SlotSurfaceWidth      : 14;
    NvU32 SlotSurfaceHeight     : 14;
    NvU32 reserved1             :  4;
    NvU32 SlotLumaWidth         : 14;
    NvU32 SlotLumaHeight        : 14;
    NvU32 reserved2             :  4;
    NvU32 SlotChromaWidth       : 14;
    NvU32 SlotChromaHeight      : 14;
    NvU32 reserved3             :  4;
} VicSlotStructSlotSurfaceConfig;

typedef struct VicSlotStructLumaKeyStruct {
    NvU64 luma_coeff0           : 20;
    NvU64 luma_coeff1           : 20;
    NvU64 luma_coeff2           : 20;
    NvU64 luma_r_shift          :  4;
    NvU64 luma_coeff3           : 20;
    NvU64 LumaKeyLower          : 10;
    NvU64 LumaKeyUpper          : 10;
    NvU64 LumaKeyEnabled        :  1;
    NvU64 reserved0             :  2;
    NvU64 reserved1             : 21;
} VicSlotStructLumaKeyStruct;

typedef struct VicSlotStructBlendingSlotStruct {
    NvU32 AlphaK1               : 10;
    NvU32 reserved0             :  6;
    NvU32 AlphaK2               : 10;
    NvU32 reserved1             :  6;
    NvU32 SrcFactCMatchSelect   :  3;
    NvU32 reserved2             :  1;
    NvU32 DstFactCMatchSelect   :  3;
    NvU32 reserved3             :  1;
    NvU32 SrcFactAMatchSelect   :  3;
    NvU32 reserved4             :  1;
    NvU32 DstFactAMatchSelect   :  3;
    NvU32 reserved5             :  1;
    NvU32 reserved6             :  4;
    NvU32 reserved7             :  4;
    NvU32 reserved8             :  4;
    NvU32 reserved9             :  4;
    NvU32 reserved10            :  2;
    NvU32 OverrideR             : 10;
    NvU32 OverrideG             : 10;
    NvU32 OverrideB             : 10;
    NvU32 OverrideA             : 10;
    NvU32 reserved11            :  2;
    NvU32 UseOverrideR          :  1;
    NvU32 UseOverrideG          :  1;
    NvU32 UseOverrideB          :  1;
    NvU32 UseOverrideA          :  1;
    NvU32 MaskR                 :  1;
    NvU32 MaskG                 :  1;
    NvU32 MaskB                 :  1;
    NvU32 MaskA                 :  1;
    NvU32 reserved12            : 12;
} VicSlotStructBlendingSlotStruct;

typedef struct VicSlotStruct {
    VicSlotStructSlotConfig         slotConfig;
    VicSlotStructSlotSurfaceConfig  slotSurfaceConfig;
    VicSlotStructLumaKeyStruct      lumaKeyStruct;
    VicMatrixStruct                 colorMatrixStruct;
    VicMatrixStruct                 gamutMatrixStruct;
    VicSlotStructBlendingSlotStruct blendingSlotStruct;
} VicSlotStruct;

typedef struct VicConfigStruct {
    VicPipeConfig                   pipeConfig;
    VicOutputConfig                 outputConfig;
    VicOutputSurfaceConfig          outputSurfaceConfig;
    VicMatrixStruct                 outColorMatrixStruct;
    VicClearRectStruct              clearRectStruct[4];
    VicSlotStruct                   slotStruct[8];
} VicConfigStruct;

#endif // __VIC_DRV_H
