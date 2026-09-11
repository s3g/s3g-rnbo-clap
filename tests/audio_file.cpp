#include "../src/s3g_rnbo_audio_file.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
int main()
{
    namespace fs = std::filesystem;
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = fs::temp_directory_path() / ("s3g-rnbo-audio-" + std::to_string(token));
    if (!fs::create_directory(directory))
        return 2;
    const auto filename = directory / fs::u8path("音声 café.wav");
    std::vector<unsigned char> wav;
    auto text = [&](const char* s) {
        for (unsigned i = 0; i < 4; ++i)
            wav.push_back(s[i]);
    };
    auto u16 = [&](unsigned x) {
        wav.push_back(x & 255);
        wav.push_back((x >> 8) & 255);
    };
    auto u32 = [&](unsigned x) {
        u16(x & 65535);
        u16(x >> 16);
    };
    text("RIFF");
    u32(36 + 16 * 2 * 2);
    text("WAVE");
    text("fmt ");
    u32(16);
    u16(1);
    u16(2);
    u32(48000);
    u32(48000 * 4);
    u16(4);
    u16(16);
    text("data");
    u32(16 * 2 * 2);
    for (unsigned i = 0; i < 16; ++i) {
        u16(8192);
        u16(0xc000);
    }
    {
        std::ofstream file(filename, std::ios::binary);
        file.write(reinterpret_cast<const char*>(wav.data()), wav.size());
    }
    s3g::rnbo_lab::DecodedAudio audio;
    std::string error;
    const bool decoded = s3g::rnbo_lab::decodeAudioFile(filename.u8string(), audio, error);
    const bool correct = decoded && audio.channels == 2 && audio.sampleCount == 32
        && audio.sampleRate == 48000. && std::abs(audio.samples[0] - .25f) < 1.e-6
        && std::abs(audio.samples[1] + .5f) < 1.e-6;
    auto* preserved = audio.samples.get();
    const bool invalidRejected
        = !s3g::rnbo_lab::decodeAudioFile((directory / "missing.wav").u8string(), audio, error)
        && audio.samples.get() == preserved;
    fs::remove(filename);
    fs::remove(directory);
    if (!correct || !invalidRejected) {
        std::cerr << "Audio decode/Unicode/failed-load check failed: " << error << '\n';
        return 1;
    }
    std::cout << "Audio decode, channel order, Unicode path and failed-load preservation passed\n";
}
