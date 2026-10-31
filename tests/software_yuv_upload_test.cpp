#include "../app/src/stream/deko3d/SoftwareYuvUpload.hpp"

#include <array>
#include <cassert>

int main()
{
    constexpr int width = 4;
    constexpr int height = 4;
    const std::array<uint8_t, 24> y = {
        1, 2, 3, 4, 99, 99,
        5, 6, 7, 8, 99, 99,
        9, 10, 11, 12, 99, 99,
        13, 14, 15, 16, 99, 99,
    };
    const std::array<uint8_t, 6> u = {21, 22, 99, 23, 24, 99};
    const std::array<uint8_t, 6> v = {31, 32, 99, 33, 34, 99};
    std::array<uint8_t, 16> output_y {};
    std::array<uint8_t, 8> output_uv {};

    assert(opennow::video::CopyYuv420ToNv12(
        width, height, y.data(), 6, u.data(), 3, v.data(), 3, false,
        output_y.data(), output_uv.data()));
    assert((output_y == std::array<uint8_t, 16>{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}));
    assert((output_uv == std::array<uint8_t, 8>{21, 31, 22, 32, 23, 33, 24, 34}));

    const std::array<uint8_t, 12> nv12 = {
        41, 51, 42, 52, 99, 99,
        43, 53, 44, 54, 99, 99,
    };
    output_uv.fill(0);
    assert(opennow::video::CopyYuv420ToNv12(
        width, height, y.data(), 6, nv12.data(), 6, nullptr, 0, true,
        output_y.data(), output_uv.data()));
    assert((output_uv == std::array<uint8_t, 8>{41, 51, 42, 52, 43, 53, 44, 54}));

    const std::array<uint8_t, 16> packed_y = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const std::array<uint8_t, 8> packed_nv12 = {41, 51, 42, 52, 43, 53, 44, 54};
    output_y.fill(0);
    output_uv.fill(0);
    assert(opennow::video::CopyYuv420ToNv12(
        width, height, packed_y.data(), width, packed_nv12.data(), width,
        nullptr, 0, true, output_y.data(), output_uv.data()));
    assert(output_y == packed_y);
    assert(output_uv == packed_nv12);

    output_y.fill(77);
    output_uv.fill(88);
    assert(!opennow::video::CopyYuv420ToNv12(
        width, height, y.data(), 2, nv12.data(), 6, nullptr, 0, true,
        output_y.data(), output_uv.data()));
    assert(!opennow::video::CopyYuv420ToNv12(
        width, height, y.data(), 6, u.data(), 1, v.data(), 3, false,
        output_y.data(), output_uv.data()));
    for (auto value : output_y) assert(value == 77);
    for (auto value : output_uv) assert(value == 88);
    assert(opennow::video::CopyYuv420ToNv12(
        width, height, y.data() + 18, -6, nv12.data() + 6, -6, nullptr, 0, true,
        output_y.data(), output_uv.data()));
    assert(output_y[0] == 13 && output_y[12] == 1);
    return 0;
}
