#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opennow::video
{

inline bool CopyYuv420ToNv12(
    int width, int height,
    const uint8_t* y_plane, ptrdiff_t y_stride,
    const uint8_t* u_or_uv_plane, ptrdiff_t u_or_uv_stride,
    const uint8_t* v_plane, ptrdiff_t v_stride,
    bool source_is_nv12,
    uint8_t* output_y, uint8_t* output_uv)
{
    if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0 ||
        !y_plane || !u_or_uv_plane || !output_y || !output_uv ||
        (!source_is_nv12 && !v_plane)) {
        return false;
    }

    // Validate before writing either destination. A smaller source frame must
    // never be read with copy extents from an older, larger stream resolution.
    const auto fits = [](ptrdiff_t stride, int row_bytes) {
        return stride >= row_bytes || stride <= -static_cast<ptrdiff_t>(row_bytes);
    };
    if (!fits(y_stride, width) ||
        !fits(u_or_uv_stride, source_is_nv12 ? width : width / 2) ||
        (!source_is_nv12 && !fits(v_stride, width / 2)))
        return false;

    // NVDEC's software transfer normally returns tightly packed NV12. Copy
    // each complete plane in one operation instead of issuing 1080 small
    // memcpy calls for every 720p frame.
    if (source_is_nv12 && y_stride == width && u_or_uv_stride == width) {
        std::memcpy(output_y, y_plane,
                    static_cast<size_t>(width) * static_cast<size_t>(height));
        std::memcpy(output_uv, u_or_uv_plane,
                    static_cast<size_t>(width) * static_cast<size_t>(height / 2));
        return true;
    }

    for (int row = 0; row < height; ++row) {
        std::memcpy(output_y + static_cast<size_t>(row) * width,
                    y_plane + static_cast<ptrdiff_t>(row) * y_stride,
                    static_cast<size_t>(width));
    }

    const int chroma_height = height / 2;
    if (source_is_nv12) {
        for (int row = 0; row < chroma_height; ++row) {
            std::memcpy(output_uv + static_cast<size_t>(row) * width,
                        u_or_uv_plane + static_cast<ptrdiff_t>(row) * u_or_uv_stride,
                        static_cast<size_t>(width));
        }
        return true;
    }

    for (int row = 0; row < chroma_height; ++row) {
        const uint8_t* u = u_or_uv_plane + static_cast<ptrdiff_t>(row) * u_or_uv_stride;
        const uint8_t* v = v_plane + static_cast<ptrdiff_t>(row) * v_stride;
        uint8_t* uv = output_uv + static_cast<size_t>(row) * width;
        for (int column = 0; column < width / 2; ++column) {
            uv[column * 2] = u[column];
            uv[column * 2 + 1] = v[column];
        }
    }
    return true;
}

} // namespace opennow::video
