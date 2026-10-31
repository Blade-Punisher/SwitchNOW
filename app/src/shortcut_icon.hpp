#pragma once
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
namespace opennow::shortcut {
inline constexpr int kArtworkSourceWidth = 1024;
inline constexpr const char* kArtworkPipeline = "area-hq-v2";
std::vector<std::uint8_t> MakeSquareJpeg(const std::string& encoded,
    const std::function<void(const std::vector<uint8_t>&)>& on_pixels = {});
}
