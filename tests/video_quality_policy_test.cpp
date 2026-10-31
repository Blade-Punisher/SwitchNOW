#include "video_quality_policy.hpp"
#include <cassert>
int main() {
    const auto performance = opennow::video::ResolveQualityTuning("Performance");
    const auto balanced = opennow::video::ResolveQualityTuning("Balanced");
    const auto quality = opennow::video::ResolveQualityTuning("Quality");
    assert(performance.denoise_strength == 0 && performance.sharpen_strength == 0);
    assert(balanced.initial_bitrate_percent > performance.initial_bitrate_percent);
    assert(balanced.sharpen_strength == 0);
    assert(quality.sharpen_strength <= .04f);
    assert(performance.preserve_reference_deblocking && quality.preserve_reference_deblocking);
    assert(opennow::video::NextQualityMode("Performance") == "Balanced");
    assert(opennow::video::NextQualityMode("Balanced") == "Quality");
    assert(opennow::video::NextQualityMode("Quality") == "Performance");
}
