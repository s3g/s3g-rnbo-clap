#include "s3g_rnbo_audio_file.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#elif defined(_WIN32)
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
#endif

namespace s3g::rnbo_lab {
namespace {
    constexpr size_t maximumSamples = 512u * 1024u * 1024u / sizeof(float);
    bool allocate(
        DecodedAudio& audio, uint64_t frames, uint32_t channels, double rate, std::string& error)
    {
        if (channels == 0 || channels > 256 || frames < 2 || frames > maximumSamples / channels
            || !std::isfinite(rate) || rate <= 0.) {
            error = "INVALID OR OVERSIZED AUDIO (512 MB LIMIT)";
            return false;
        }
        audio.sampleCount = size_t(frames) * channels;
        audio.channels = channels;
        audio.sampleRate = rate;
        audio.samples = std::make_unique<float[]>(audio.sampleCount);
        return true;
    }
}
bool decodeAudioFile(const std::string& path, DecodedAudio& result, std::string& error)
{
    if (path.empty() || path.find('\0') != std::string::npos) {
        error = "INVALID AUDIO PATH";
        return false;
    }
    try {
        DecodedAudio audio;
#if defined(__APPLE__)
        @autoreleasepool {
            NSString* name = [NSString stringWithUTF8String:path.c_str()];
            if (!name) {
                error = "INVALID UTF-8 PATH";
                return false;
            }
            NSError* nativeError = nil;
            AVAudioFile* file = [[AVAudioFile alloc] initForReading:[NSURL fileURLWithPath:name]
                                                              error:&nativeError];
            if (!file) {
                error = "COULD NOT OPEN AUDIO";
                return false;
            }
            struct FileGuard {
                AVAudioFile* file;
                ~FileGuard() { [file release]; }
            } fileGuard { file };
            AVAudioFormat* format = [file processingFormat];
            if (!allocate(audio, [file length], format.channelCount, format.sampleRate, error))
                return false;
            const auto frames = static_cast<AVAudioFrameCount>(audio.sampleCount / audio.channels);
            AVAudioPCMBuffer* buffer =
                [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:frames];
            struct BufferGuard {
                AVAudioPCMBuffer* buffer;
                ~BufferGuard() { [buffer release]; }
            } bufferGuard { buffer };
            if (!buffer || ![file readIntoBuffer:buffer error:&nativeError]
                || buffer.frameLength != frames || !buffer.floatChannelData) {
                error = "AUDIO READ FAILED";
                return false;
            }
            for (size_t frame = 0; frame < frames; ++frame)
                for (uint32_t channel = 0; channel < audio.channels; ++channel)
                    audio.samples[frame * audio.channels + channel]
                        = buffer.floatChannelData[channel][frame];
        }
#elif defined(_WIN32)
        const auto native = std::filesystem::u8path(path);
        drwav decoder {};
        if (!drwav_init_file_w(&decoder, native.c_str(), nullptr)) {
            error = "COULD NOT OPEN WAV OR AIFF";
            return false;
        }
        struct Guard {
            drwav* decoder;
            ~Guard() { drwav_uninit(decoder); }
        } guard { &decoder };
        if (!allocate(
                audio, decoder.totalPCMFrameCount, decoder.channels, decoder.sampleRate, error))
            return false;
        if (drwav_read_pcm_frames_f32(&decoder, decoder.totalPCMFrameCount, audio.samples.get())
            != decoder.totalPCMFrameCount) {
            error = "AUDIO READ FAILED";
            return false;
        }
#else
        error = "AUDIO FILE DECODING REQUIRES MACOS OR WINDOWS";
        return false;
#endif
        for (size_t i = 0; i < audio.sampleCount; ++i)
            if (!std::isfinite(audio.samples[i])) {
                error = "NON-FINITE AUDIO DATA";
                return false;
            }
        result = std::move(audio);
        error.clear();
        return true;
    } catch (...) {
        error = "AUDIO DECODE FAILED OR OUT OF MEMORY";
        return false;
    }
}
}
