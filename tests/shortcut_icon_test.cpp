#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include "shortcut_icon.hpp"
#include <cassert>
#include <string>
#include <vector>
static void Append(void* ctx, void* bytes, int count) {
    static_cast<std::string*>(ctx)->append(static_cast<char*>(bytes), count);
}
int main() {
    using opennow::shortcut::MakeSquareJpeg;
    assert(MakeSquareJpeg("").empty());
    assert(MakeSquareJpeg("not an image").empty());
    std::vector<unsigned char> pixels(512 * 128 * 4, 128);
    std::string png;
    assert(stbi_write_png_to_func(Append, &png, 512, 128, 4, pixels.data(), 0));
    const auto icon = MakeSquareJpeg(png);
    assert(!icon.empty() && icon.size() < 0x20000);
    int w=0, h=0, c=0;
    auto* decoded = stbi_load_from_memory(icon.data(), static_cast<int>(icon.size()), &w, &h, &c, 3);
    assert(decoded && w == 256 && h == 256);
    stbi_image_free(decoded);
    assert(MakeSquareJpeg(png) == icon);
    assert(MakeSquareJpeg(png.substr(0, 20)).empty());
    // Fine alternating pixels must average to grey, not alias to black.
    std::vector<unsigned char> checker(512 * 512 * 3);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x)
            for (int ch = 0; ch < 3; ++ch)
                checker[(y * 512 + x) * 3 + ch] = ((x + y) % 2) ? 255 : 0;
    std::string checker_png;
    assert(stbi_write_png_to_func(Append, &checker_png, 512, 512, 3, checker.data(), 0));
    auto antialiased = MakeSquareJpeg(checker_png, [](const auto& rgb) {
        for (auto value : rgb) assert(value >= 127 && value <= 128);
    });
    assert(!antialiased.empty() && antialiased.size() < 0x20000);
    // Non-integral 576 -> 256 resize must preserve a constant centre crop,
    // excluding contrasting side panels of the high-resolution banner.
    std::vector<unsigned char> banner(1024 * 576 * 3, 0);
    for (int y = 0; y < 576; ++y)
        for (int x = 224; x < 800; ++x)
            for (int ch = 0; ch < 3; ++ch) banner[(y * 1024 + x) * 3 + ch] = 173;
    std::string banner_png;
    assert(stbi_write_png_to_func(Append, &banner_png, 1024, 576, 3, banner.data(), 0));
    assert(!MakeSquareJpeg(banner_png, [](const auto& rgb) {
        for (auto value : rgb) assert(value == 173);
    }).empty());
    // A tiny gradient must interpolate between source pixels when enlarged.
    const unsigned char gradient[] = {0,0,0,255,255,255,0,0,0,255,255,255};
    std::string small;
    assert(stbi_write_png_to_func(Append, &small, 2, 2, 3, gradient, 0));
    assert(!MakeSquareJpeg(small, [](const auto& rgb) {
        assert(rgb[(128 * 256 + 128) * 3] >= 127);
        assert(rgb[(128 * 256 + 128) * 3] <= 130);
        assert(rgb.front() == 0 && rgb.back() == 255);
    }).empty());
    // A highly compressed wide image must be rejected before RGB allocation.
    std::vector<unsigned char> wide(4097 * 4, 0);
    std::string oversized;
    assert(stbi_write_png_to_func(Append, &oversized, 4097, 1, 4, wide.data(), 0));
    assert(MakeSquareJpeg(oversized).empty());
}
