#include "s3g_rnbo_fallback.h"

#include <cstdint>
#include <cmath>
#include <iostream>

int main()
{
    constexpr uint32_t channels = 8;
    float input[channels] {};
    float output[channels] {};
    for (uint32_t ch = 0; ch < channels; ++ch) {
        input[ch] = (ch & 1u) ? -0.25f : 0.25f;
    }
    s3g::rnbo_lab::processFallbackFrame(input, output, channels, channels, 0.75f, 1.0f);
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (!std::isfinite(output[ch])) {
            std::cerr << "non-finite fallback output\n";
            return 1;
        }
    }
    std::cout << "s3g-rnbo-clap smoke passed: " << output[0] << " / " << output[7] << "\n";
    return 0;
}
