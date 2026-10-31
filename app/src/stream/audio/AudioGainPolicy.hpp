#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace opennow::audio {
struct GainEnvelope {
    float gain = 1.0f;
    unsigned hold_samples = 0;
    float Update(int input_peak, float requested, unsigned frames, unsigned rate = 48000) {
        requested = std::clamp(requested, 1.0f, 8.0f);
        const float safe = input_peak > 0 && requested > 1.0f
            ? std::min(requested, 30000.0f / static_cast<float>(input_peak)) : requested;
        if (safe < gain) {
            gain = safe; hold_samples = rate / 5; // 200ms hold after peak attack.
        } else if (hold_samples) {
            hold_samples = hold_samples > frames ? hold_samples - frames : 0;
        } else if (rate) {
            const float release = 1.0f - std::exp(-static_cast<float>(frames) / (rate * .5f));
            gain += (safe - gain) * release;
        }
        gain = std::clamp(gain, 0.0f, std::min(safe, requested));
        return gain;
    }
};
}
