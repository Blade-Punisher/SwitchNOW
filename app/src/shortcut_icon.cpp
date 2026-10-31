#include "shortcut_icon.hpp"
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <algorithm>
#include <limits>
#include <cmath>
#include <memory>
namespace opennow::shortcut {
void JpegWriteCallback(void* context, void* data, int size)
{
    if (!context || !data || size <= 0)
        return;
    auto& output = *static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    output.insert(output.end(), bytes, bytes + size);
}

std::vector<std::uint8_t> MakeSquareJpeg(const std::string& encoded,
    const std::function<void(const std::vector<uint8_t>&)>& on_pixels)
{
    if (encoded.empty() ||
        encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return {};

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(
            reinterpret_cast<const unsigned char*>(encoded.data()),
            static_cast<int>(encoded.size()), &width, &height, &channels) ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        static_cast<uint64_t>(width) * height > 8 * 1024 * 1024)
        return {};
    unsigned char* decoded = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(encoded.data()),
        static_cast<int>(encoded.size()), &width, &height, &channels, 3);
    if (!decoded || width <= 0 || height <= 0)
    {
        stbi_image_free(decoded);
        return {};
    }

    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(decoded, stbi_image_free);
    constexpr int kOutputSize = 256;
    const int crop_size = std::min(width, height);
    const int crop_x = (width - crop_size) / 2;
    const int crop_y = (height - crop_size) / 2;
    std::vector<std::uint8_t> square(kOutputSize * kOutputSize * 3);
    // Area filtering preserves fine detail without nearest-neighbour aliasing.
    // Small sources use pixel-centred bilinear interpolation instead of blocks.
    const double scale = static_cast<double>(crop_size) / kOutputSize;
    for (int y = 0; y < kOutputSize; ++y)
        for (int x = 0; x < kOutputSize; ++x)
            for (int c = 0; c < 3; ++c)
            {
                double value = 0;
                if (crop_size >= kOutputSize)
                {
                    const double left = x * scale, right = (x + 1) * scale;
                    const double top = y * scale, bottom = (y + 1) * scale;
                    for (int sy = static_cast<int>(top); sy < static_cast<int>(std::ceil(bottom)); ++sy)
                        for (int sx = static_cast<int>(left); sx < static_cast<int>(std::ceil(right)); ++sx)
                        {
                            const double weight =
                                (std::min(right, sx + 1.0) - std::max(left, static_cast<double>(sx))) *
                                (std::min(bottom, sy + 1.0) - std::max(top, static_cast<double>(sy)));
                            const int px = std::min(sx, crop_size - 1) + crop_x;
                            const int py = std::min(sy, crop_size - 1) + crop_y;
                            value += decoded[(static_cast<size_t>(py) * width + px) * 3 + c] * weight;
                        }
                    value /= scale * scale;
                }
                else
                {
                    const double fx = std::clamp((x + 0.5) * scale - 0.5, 0.0, crop_size - 1.0);
                    const double fy = std::clamp((y + 0.5) * scale - 0.5, 0.0, crop_size - 1.0);
                    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
                    const int x1 = std::min(x0 + 1, crop_size - 1), y1 = std::min(y0 + 1, crop_size - 1);
                    const auto pixel = [&](int px, int py) {
                        return decoded[(static_cast<size_t>(py + crop_y) * width + px + crop_x) * 3 + c];
                    };
                    value = (1 - (fy - y0)) * ((1 - (fx - x0)) * pixel(x0, y0) + (fx - x0) * pixel(x1, y0)) +
                        (fy - y0) * ((1 - (fx - x0)) * pixel(x0, y1) + (fx - x0) * pixel(x1, y1));
                }
                square[(static_cast<size_t>(y) * kOutputSize + x) * 3 + c] =
                    static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
            }
    pixels.reset();
    if (on_pixels) on_pixels(square);

    for (int quality : {98, 96, 92, 86, 78, 68})
    {
        std::vector<std::uint8_t> jpeg;
        if (stbi_write_jpg_to_func(
                JpegWriteCallback, &jpeg, kOutputSize, kOutputSize, 3,
                square.data(), quality) &&
            !jpeg.empty() && jpeg.size() < 0x20000)
            return jpeg;
    }
    return {};
}

}
