#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
namespace s3g::rnbo_lab {
struct DecodedAudio {
    std::unique_ptr<float[]> samples;
    size_t sampleCount = 0;
    uint32_t channels = 0;
    double sampleRate = 0.;
};
// Main thread only. UTF-8 input, native wide paths on Windows. The same PCM
// WAV/AIFF interchange files can be used on both operating systems.
bool decodeAudioFile(const std::string& path, DecodedAudio& result, std::string& error);
}
