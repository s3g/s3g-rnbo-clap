#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g::rnbo_lab {

inline float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

inline float smoothParam(float current, float target)
{
    return current + (target - current) * 0.0015f;
}

inline void processFallbackFrame(const float* input,
                                 float* output,
                                 uint32_t inputChannels,
                                 uint32_t outputChannels,
                                 float gain,
                                 float mix)
{
    const float g = std::pow(10.0f, (gain * 36.0f - 24.0f) / 20.0f);
    for (uint32_t ch = 0; ch < outputChannels; ++ch) {
        const float dry = ch < inputChannels ? input[ch] : 0.0f;
        const float wet = std::tanh(dry * g);
        output[ch] = dry + (wet - dry) * clamp01(mix);
    }
}

} // namespace s3g::rnbo_lab
