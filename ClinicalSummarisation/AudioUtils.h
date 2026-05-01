#pragma once
#include <vector>
#include <algorithm>
#include <cmath>

namespace AudioUtils {
    inline void NormaliseAudio(std::vector<float>& audioData) {
        float max_amp = 0.0f;
        for (float s : audioData) max_amp = std::max(max_amp, std::abs(s));

        if (max_amp > 0.001f && max_amp < 0.9f) {
            float gain = 0.9f / max_amp;
            for (float& s : audioData) s *= gain;
        }
    }
}
