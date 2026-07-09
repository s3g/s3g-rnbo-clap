#include "s3g_rnbo_fallback.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif

#if S3G_HAS_RNBO_EXPORT
#include "RNBO.h"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

#ifndef S3G_RNBO_PLUGIN_ID
#define S3G_RNBO_PLUGIN_ID "org.s3g.s3g-rnbo-clap.8ch-passthru"
#endif
#ifndef S3G_RNBO_PLUGIN_NAME
#define S3G_RNBO_PLUGIN_NAME "s3g RNBO 8ch Passthru"
#endif
#ifndef S3G_RNBO_PLUGIN_DESCRIPTION
#define S3G_RNBO_PLUGIN_DESCRIPTION "Eight-channel CLAP wrapper test for an RNBO C++ passthrough export."
#endif

namespace {

constexpr uint32_t kInputChannels = S3G_RNBO_INPUT_CHANNELS;
constexpr uint32_t kOutputChannels = S3G_RNBO_OUTPUT_CHANNELS;
constexpr uint32_t kStateVersion = 1;
constexpr clap_id kRnboParamBase = 1000;

enum ParamId : clap_id {
    kGain = 1,
    kMix,
    kOutput,
};

struct Params {
    float gain = 2.0f / 3.0f;
    float mix = 1.0f;
    float output = 1.0f;
};

struct SavedState {
    uint32_t version = kStateVersion;
    Params params {};
};

struct StateHeader {
    uint32_t version = kStateVersion;
    uint32_t count = 0;
};

#if S3G_HAS_RNBO_EXPORT
struct RnboParam {
    clap_id id = CLAP_INVALID_ID;
    RNBO::ParameterIndex index = 0;
    std::string name;
    std::string module = "RNBO";
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;
    int steps = 0;
    std::vector<std::string> enumValues;
};
#endif

struct RnboProcessor {
    double sampleRate = 48000.0;
    uint32_t maxBlock = 1024;
    std::vector<float> fallbackIn;
    std::vector<float> fallbackOut;
    float gainSmooth = 2.0f / 3.0f;
    float mixSmooth = 1.0f;
    float outSmooth = 1.0f;
    uint32_t startupSilenceFrames = 1;
    uint32_t startupSilenceRemaining = 0;
    uint32_t startupRampFrames = 1;
    uint32_t startupRampRemaining = 0;
#if S3G_HAS_RNBO_EXPORT
    RNBO::CoreObject rnbo {};
    std::vector<RNBO::SampleValue> inputStorage;
    std::vector<RNBO::SampleValue> outputStorage;
    std::vector<RNBO::SampleValue*> inputPtrs;
    std::vector<RNBO::SampleValue*> outputPtrs;
    RNBO::MidiEventList midiInput;
    RNBO::MidiEventList midiOutput;
#endif

    void prepare(double sr, uint32_t blockSize)
    {
        sampleRate = std::max(1.0, sr);
        maxBlock = std::max<uint32_t>(1u, blockSize);
        startupSilenceFrames = std::max<uint32_t>(1u, static_cast<uint32_t>(std::round(sampleRate * 0.075)));
        startupRampFrames = std::max<uint32_t>(1u, static_cast<uint32_t>(std::round(sampleRate * 0.175)));
        armStartupGuard();
        fallbackIn.assign(kInputChannels, 0.0f);
        fallbackOut.assign(kOutputChannels, 0.0f);
#if S3G_HAS_RNBO_EXPORT
        rnbo.prepareToProcess(sampleRate, maxBlock);
        inputStorage.assign(kInputChannels * maxBlock, 0.0);
        outputStorage.assign(kOutputChannels * maxBlock, 0.0);
        inputPtrs.resize(kInputChannels);
        outputPtrs.resize(kOutputChannels);
        for (uint32_t ch = 0; ch < kInputChannels; ++ch) inputPtrs[ch] = inputStorage.data() + ch * maxBlock;
        for (uint32_t ch = 0; ch < kOutputChannels; ++ch) outputPtrs[ch] = outputStorage.data() + ch * maxBlock;
        settleRnbo();
#endif
    }

    void armStartupGuard()
    {
        startupSilenceRemaining = startupSilenceFrames;
        startupRampRemaining = startupRampFrames;
    }

    void reset()
    {
        std::fill(fallbackIn.begin(), fallbackIn.end(), 0.0f);
        std::fill(fallbackOut.begin(), fallbackOut.end(), 0.0f);
        gainSmooth = 2.0f / 3.0f;
        mixSmooth = 1.0f;
        outSmooth = 1.0f;
        armStartupGuard();
#if S3G_HAS_RNBO_EXPORT
        midiInput.clear();
        midiOutput.clear();
        std::fill(inputStorage.begin(), inputStorage.end(), 0.0);
        std::fill(outputStorage.begin(), outputStorage.end(), 0.0);
        rnbo.prepareToProcess(sampleRate, maxBlock);
        settleRnbo();
#endif
    }

    float nextStartupGain()
    {
        if (startupSilenceRemaining > 0) {
            --startupSilenceRemaining;
            return 0.0f;
        }
        if (startupRampRemaining == 0 || startupRampFrames == 0) return 1.0f;
        const uint32_t elapsed = startupRampFrames - startupRampRemaining;
        --startupRampRemaining;
        const float linear = static_cast<float>(elapsed) / static_cast<float>(startupRampFrames);
        return linear * linear;
    }

    void applyStartupRamp(const clap_audio_buffer_t& output, uint32_t frames)
    {
        if (startupSilenceRemaining == 0 && startupRampRemaining == 0) return;
        for (uint32_t i = 0; i < frames; ++i) {
            const float gain = nextStartupGain();
            if (gain >= 1.0f) continue;
            for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
                if (output.data32 && output.data32[ch]) output.data32[ch][i] *= gain;
                if (output.data64 && output.data64[ch]) output.data64[ch][i] *= static_cast<double>(gain);
            }
        }
    }

#if S3G_HAS_RNBO_EXPORT
    void settleRnbo()
    {
        if (maxBlock == 0) return;
        midiInput.clear();
        midiOutput.clear();
        const uint32_t settleFrames = std::max<uint32_t>(maxBlock, static_cast<uint32_t>(std::round(sampleRate * 0.500)));
        uint32_t remaining = settleFrames;
        while (remaining > 0) {
            const uint32_t n = std::min<uint32_t>(remaining, maxBlock);
            for (uint32_t ch = 0; ch < kInputChannels; ++ch) std::fill(inputPtrs[ch], inputPtrs[ch] + n, 0.0);
            for (uint32_t ch = 0; ch < kOutputChannels; ++ch) std::fill(outputPtrs[ch], outputPtrs[ch] + n, 0.0);
            rnbo.process(inputPtrs.data(), kInputChannels, outputPtrs.data(), kOutputChannels, n, &midiInput, &midiOutput);
            midiInput.clear();
            midiOutput.clear();
            remaining -= n;
        }
        std::fill(outputStorage.begin(), outputStorage.end(), 0.0);
    }
#endif

    void processFallback(const clap_audio_buffer_t& input,
                         const clap_audio_buffer_t& output,
                         uint32_t frame,
                         const Params& params)
    {
        gainSmooth = s3g::rnbo_lab::smoothParam(gainSmooth, params.gain);
        mixSmooth = s3g::rnbo_lab::smoothParam(mixSmooth, params.mix);
        outSmooth = s3g::rnbo_lab::smoothParam(outSmooth, params.output);
        for (uint32_t ch = 0; ch < kInputChannels; ++ch) {
            fallbackIn[ch] = 0.0f;
            if (ch < input.channel_count && input.data32 && input.data32[ch]) fallbackIn[ch] = input.data32[ch][frame];
            else if (ch < input.channel_count && input.data64 && input.data64[ch]) fallbackIn[ch] = static_cast<float>(input.data64[ch][frame]);
        }
        s3g::rnbo_lab::processFallbackFrame(fallbackIn.data(), fallbackOut.data(), kInputChannels, kOutputChannels, gainSmooth, mixSmooth);
        for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
            const float value = ch < kOutputChannels ? fallbackOut[ch] * outSmooth : 0.0f;
            if (output.data32 && output.data32[ch]) output.data32[ch][frame] = value;
            if (output.data64 && output.data64[ch]) output.data64[ch][frame] = static_cast<double>(value);
        }
    }

#if S3G_HAS_RNBO_EXPORT
    void processRnbo(const clap_audio_buffer_t& input,
                     const clap_audio_buffer_t& output,
                     uint32_t frames)
    {
        const uint32_t n = std::min(frames, maxBlock);
        for (uint32_t ch = 0; ch < kInputChannels; ++ch) {
            RNBO::SampleValue* dst = inputPtrs[ch];
            for (uint32_t i = 0; i < n; ++i) {
                RNBO::SampleValue value = 0.0;
                if (ch < input.channel_count && input.data32 && input.data32[ch]) value = input.data32[ch][i];
                else if (ch < input.channel_count && input.data64 && input.data64[ch]) value = input.data64[ch][i];
                dst[i] = value;
            }
        }
        for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
            std::fill(outputPtrs[ch], outputPtrs[ch] + n, 0.0);
        }

        rnbo.process(inputPtrs.data(), kInputChannels, outputPtrs.data(), kOutputChannels, n, &midiInput, &midiOutput);
        midiInput.clear();
        midiOutput.clear();

        for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
            for (uint32_t i = 0; i < n; ++i) {
                const float value = ch < kOutputChannels ? static_cast<float>(outputPtrs[ch][i]) : 0.0f;
                if (output.data32 && output.data32[ch]) output.data32[ch][i] = value;
                if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(value);
            }
        }
    }
#endif
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    Params params {};
    RnboProcessor processor {};
#if S3G_HAS_RNBO_EXPORT
    std::vector<RnboParam> rnboParams;
#endif
    std::atomic<float> peak { 0.0f };
    std::atomic<float> midiActivity { 0.0f };
    std::atomic<float> randomAmount { 1.0f };
    uint32_t randomSeed = 0x63a85f2bu;
#if defined(__APPLE__)
    void* guiView = nullptr;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

#if S3G_HAS_RNBO_EXPORT
void discoverRnboParams(Plugin& p)
{
    p.rnboParams.clear();
    const auto count = p.processor.rnbo.getNumParameters();
    p.rnboParams.reserve(static_cast<size_t>(count));
    for (RNBO::ParameterIndex i = 0; i < count; ++i) {
        RNBO::ParameterInfo rnboInfo {};
        p.processor.rnbo.getParameterInfo(i, &rnboInfo);
        if (!rnboInfo.visible) continue;

        RnboParam param {};
        param.id = kRnboParamBase + static_cast<clap_id>(i);
        param.index = i;
        const char* displayName = rnboInfo.displayName && rnboInfo.displayName[0] ? rnboInfo.displayName : nullptr;
        const char* name = displayName ? displayName : p.processor.rnbo.getParameterName(i);
        param.name = name && name[0] ? name : p.processor.rnbo.getParameterId(i);
        param.min = rnboInfo.min;
        param.max = rnboInfo.max;
        param.defaultValue = rnboInfo.initialValue;
        param.steps = rnboInfo.steps;
        if (rnboInfo.enumValues && rnboInfo.steps > 0) {
            param.enumValues.reserve(static_cast<size_t>(rnboInfo.steps));
            for (int step = 0; step < rnboInfo.steps; ++step) {
                const char* value = rnboInfo.enumValues[step];
                if (value && value[0]) param.enumValues.emplace_back(value);
            }
        }
        p.rnboParams.push_back(std::move(param));
    }
}

RnboParam* findRnboParam(Plugin& p, clap_id id)
{
    for (auto& param : p.rnboParams) {
        if (param.id == id) return &param;
    }
    return nullptr;
}

const RnboParam* findRnboParam(const Plugin& p, clap_id id)
{
    for (const auto& param : p.rnboParams) {
        if (param.id == id) return &param;
    }
    return nullptr;
}

bool isEnumParam(const RnboParam& param)
{
    return !param.enumValues.empty();
}

double steppedParamValue(const RnboParam& param, double value)
{
    if (isEnumParam(param)) {
        const auto last = static_cast<int>(param.enumValues.size()) - 1;
        const int index = std::clamp(static_cast<int>(std::lround(value - param.min)), 0, std::max(0, last));
        return param.min + index;
    }
    if (param.steps > 1 && param.max > param.min) {
        const double normalized = std::clamp((value - param.min) / (param.max - param.min), 0.0, 1.0);
        const double step = std::round(normalized * static_cast<double>(param.steps - 1));
        return param.min + (step / static_cast<double>(param.steps - 1)) * (param.max - param.min);
    }
    if (param.steps == 1) return value >= 0.5 * (param.min + param.max) ? param.max : param.min;
    return value;
}
#endif

float clampParam(clap_id id, double value)
{
#if S3G_HAS_RNBO_EXPORT
    (void)id;
    return static_cast<float>(value);
#else
    if (id == kGain || id == kMix || id == kOutput) return static_cast<float>(std::clamp(value, 0.0, 1.0));
    return static_cast<float>(value);
#endif
}

void setParam(Plugin& p, clap_id id, double value)
{
#if S3G_HAS_RNBO_EXPORT
    if (auto* param = findRnboParam(p, id)) {
        const double constrained = std::clamp(value, param->min, param->max);
        p.processor.rnbo.setParameterValue(param->index, constrained);
#if defined(__APPLE__)
        if (p.guiView) [static_cast<NSView*>(p.guiView) setNeedsDisplay:YES];
#endif
    }
#else
    const float v = clampParam(id, value);
    if (id == kGain) p.params.gain = v;
    else if (id == kMix) p.params.mix = v;
    else if (id == kOutput) p.params.output = v;
    else return;
#if defined(__APPLE__)
    if (p.guiView) [static_cast<NSView*>(p.guiView) setNeedsDisplay:YES];
#endif
#endif
}

double getParam(Plugin& p, clap_id id)
{
#if S3G_HAS_RNBO_EXPORT
    if (const auto* param = findRnboParam(p, id)) {
        return p.processor.rnbo.getParameterValue(param->index);
    }
    return 0.0;
#else
    if (id == kGain) return p.params.gain;
    if (id == kMix) return p.params.mix;
    if (id == kOutput) return p.params.output;
    return 0.0;
#endif
}

double random01(Plugin& p)
{
    uint32_t x = p.randomSeed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p.randomSeed = x ? x : 0x63a85f2bu;
    return static_cast<double>(p.randomSeed & 0x00ffffffu) / static_cast<double>(0x01000000u);
}

void randomizeParams(Plugin& p)
{
    const double amount = std::clamp(static_cast<double>(p.randomAmount.load(std::memory_order_relaxed)), 0.0, 1.0);
#if S3G_HAS_RNBO_EXPORT
    for (const auto& param : p.rnboParams) {
        const double current = p.processor.rnbo.getParameterValue(param.index);
        double target = param.min;
        double value = current;
        if (isEnumParam(param)) {
            const int last = std::max(0, static_cast<int>(param.enumValues.size()) - 1);
            target = param.min + static_cast<double>(std::clamp(static_cast<int>(std::floor(random01(p) * (last + 1))), 0, last));
            value = random01(p) < amount ? target : current;
        } else if (param.steps > 1 && param.max > param.min) {
            const int step = std::clamp(static_cast<int>(std::floor(random01(p) * param.steps)), 0, param.steps - 1);
            target = param.min + (static_cast<double>(step) / static_cast<double>(param.steps - 1)) * (param.max - param.min);
            value = steppedParamValue(param, current + (target - current) * amount);
        } else if (param.steps == 1) {
            target = random01(p) >= 0.5 ? param.max : param.min;
            value = random01(p) < amount ? target : current;
        } else {
            target = param.min + random01(p) * (param.max - param.min);
            value = current + (target - current) * amount;
        }
        setParam(p, param.id, value);
    }
#else
    setParam(p, kGain, p.params.gain + (random01(p) - p.params.gain) * amount);
    setParam(p, kMix, p.params.mix + (random01(p) - p.params.mix) * amount);
    setParam(p, kOutput, p.params.output + (random01(p) - p.params.output) * amount);
#endif
}

#if S3G_HAS_RNBO_EXPORT
void addRnboMidiEvent(Plugin& p, uint32_t sampleOffset, const uint8_t* data, RNBO::Index length)
{
    if (!data || length == 0 || p.processor.rnbo.getNumMidiInputPorts() <= 0) return;
    const RNBO::MillisecondTime t = (static_cast<RNBO::MillisecondTime>(sampleOffset) / p.processor.sampleRate) * 1000.0;
    p.processor.midiInput.addEvent(RNBO::MidiEvent(t, 0, data, length));
    p.midiActivity.store(1.0f, std::memory_order_relaxed);
}
#endif

void readEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (!ev || ev->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            setParam(p, param->param_id, param->value);
        }
#if S3G_HAS_RNBO_EXPORT
        else if (ev->type == CLAP_EVENT_MIDI) {
            const auto* midi = reinterpret_cast<const clap_event_midi_t*>(ev);
            addRnboMidiEvent(p, ev->time, midi->data, 3);
        } else if (ev->type == CLAP_EVENT_NOTE_ON || ev->type == CLAP_EVENT_NOTE_OFF || ev->type == CLAP_EVENT_NOTE_CHOKE) {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(ev);
            const int channel = std::clamp(static_cast<int>(note->channel), 0, 15);
            const int key = std::clamp(static_cast<int>(note->key), 0, 127);
            const bool on = ev->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0;
            uint8_t data[3] {
                static_cast<uint8_t>((on ? 0x90 : 0x80) | channel),
                static_cast<uint8_t>(key),
                static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(note->velocity * 127.0)), 0, 127))
            };
            if (!on) data[2] = 0;
            addRnboMidiEvent(p, ev->time, data, 3);
        }
#endif
    }
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiView) {
        NSView* view = static_cast<NSView*>(p->guiView);
        if ([view respondsToSelector:@selector(stopTimer)]) [view performSelector:@selector(stopTimer)];
        [view removeFromSuperview];
        [view release];
        p->guiView = nullptr;
    }
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxBlockSize)
{
    self(plugin)->processor.prepare(sampleRate, maxBlockSize);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->processor.reset(); }

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
#if S3G_HAS_RNBO_EXPORT
    p->processor.midiInput.clear();
    p->processor.midiOutput.clear();
#endif
    readEvents(*p, proc->in_events);
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const clap_audio_buffer_t emptyInput {};
    const auto& input = proc->audio_inputs_count > 0 ? proc->audio_inputs[0] : emptyInput;
    const auto& output = proc->audio_outputs[0];
#if S3G_HAS_RNBO_EXPORT
    p->processor.processRnbo(input, output, proc->frames_count);
#else
    for (uint32_t i = 0; i < proc->frames_count; ++i) {
        p->processor.processFallback(input, output, i, p->params);
    }
#endif
    p->processor.applyStartupRamp(output, proc->frames_count);
    float blockPeak = 0.0f;
    for (uint32_t ch = 0; ch < std::min(output.channel_count, kOutputChannels); ++ch) {
        for (uint32_t i = 0; i < proc->frames_count; ++i) {
            if (output.data32 && output.data32[ch]) blockPeak = std::max(blockPeak, std::fabs(output.data32[ch][i]));
            else if (output.data64 && output.data64[ch]) blockPeak = std::max(blockPeak, static_cast<float>(std::fabs(output.data64[ch][i])));
        }
    }
    p->peak.store(std::max(blockPeak, p->peak.load(std::memory_order_relaxed) * 0.92f), std::memory_order_relaxed);
    p->midiActivity.store(p->midiActivity.load(std::memory_order_relaxed) * 0.90f, std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? (kInputChannels > 0 ? 1u : 0u) : (kOutputChannels > 0 ? 1u : 0u);
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 1 : 2;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "RNBO In" : "RNBO Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = (info->channel_count == 2) ? CLAP_PORT_STEREO : nullptr;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t* plugin, bool isInput)
{
#if S3G_HAS_RNBO_EXPORT
    return isInput && self(plugin)->processor.rnbo.getNumMidiInputPorts() > 0 ? 1u : 0u;
#else
    (void)plugin;
    (void)isInput;
    return 0u;
#endif
}

bool notePortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
#if S3G_HAS_RNBO_EXPORT
    if (!info || !isInput || index != 0 || self(plugin)->processor.rnbo.getNumMidiInputPorts() <= 0) return false;
    info->id = 30;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "MIDI In");
    return true;
#else
    (void)plugin;
    (void)index;
    (void)isInput;
    (void)info;
    return false;
#endif
}

const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

clap_param_info_t makeParam(clap_id id, const char* name, double def)
{
    clap_param_info_t info {};
    info.id = id;
    info.flags = CLAP_PARAM_IS_AUTOMATABLE;
    info.min_value = 0.0;
    info.max_value = 1.0;
    info.default_value = def;
    std::snprintf(info.name, sizeof(info.name), "%s", name);
    std::snprintf(info.module, sizeof(info.module), "%s", "Wrapper");
    return info;
}

uint32_t paramsCount(const clap_plugin_t* plugin)
{
#if S3G_HAS_RNBO_EXPORT
    return static_cast<uint32_t>(self(plugin)->rnboParams.size());
#else
    return 3;
#endif
}

bool paramsGetInfo(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info)
{
#if S3G_HAS_RNBO_EXPORT
    auto* p = self(plugin);
    if (!info || index >= p->rnboParams.size()) return false;
    const auto& param = p->rnboParams[index];
    info->id = param.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = param.min;
    info->max_value = param.max;
    info->default_value = param.defaultValue;
    if (param.steps > 0) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::snprintf(info->name, sizeof(info->name), "%s", param.name.c_str());
    std::snprintf(info->module, sizeof(info->module), "%s", param.module.c_str());
    return true;
#else
    if (!info || index >= 3) return false;
    if (index == 0) *info = makeParam(kGain, "Gain", 0.50);
    else if (index == 1) *info = makeParam(kMix, "Mix", 1.0);
    else *info = makeParam(kOutput, "Output", 1.0);
    return true;
#endif
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
#if S3G_HAS_RNBO_EXPORT
    if (!findRnboParam(*self(plugin), id)) return false;
#else
    if (id != kGain && id != kMix && id != kOutput) return false;
#endif
    *value = getParam(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
#if S3G_HAS_RNBO_EXPORT
    const auto* param = findRnboParam(*self(plugin), id);
    if (!param) return false;
    if (isEnumParam(*param)) {
        const int index = std::clamp(static_cast<int>(std::lround(value - param->min)), 0, static_cast<int>(param->enumValues.size()) - 1);
        std::snprintf(display, size, "%s", param->enumValues[static_cast<size_t>(index)].c_str());
        return true;
    }
#endif
    std::snprintf(display, size, "%.3f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
#if S3G_HAS_RNBO_EXPORT
    const auto* param = findRnboParam(*self(plugin), id);
    if (!param) return false;
    if (isEnumParam(*param)) {
        const std::string text(display);
        for (size_t i = 0; i < param->enumValues.size(); ++i) {
            if (text == param->enumValues[i]) {
                *value = param->min + static_cast<double>(i);
                return true;
            }
        }
    }
    *value = steppedParamValue(*param, std::clamp(std::atof(display), param->min, param->max));
#else
    *value = clampParam(id, std::atof(display));
#endif
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readEvents(*self(plugin), in);
}

const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool writeFull(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* cursor = static_cast<const uint8_t*>(data);
    while (size > 0) {
        const int64_t n = stream->write(stream, cursor, size);
        if (n <= 0) return false;
        cursor += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool readFull(const clap_istream_t* stream, void* data, size_t size)
{
    auto* cursor = static_cast<uint8_t*>(data);
    while (size > 0) {
        const int64_t n = stream->read(stream, cursor, size);
        if (n <= 0) return false;
        cursor += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
#if S3G_HAS_RNBO_EXPORT
    auto* p = self(plugin);
    StateHeader header {};
    header.count = static_cast<uint32_t>(p->rnboParams.size());
    if (!stream || !stream->write || !writeFull(stream, &header, sizeof(header))) return false;
    for (const auto& param : p->rnboParams) {
        const double value = p->processor.rnbo.getParameterValue(param.index);
        if (!writeFull(stream, &value, sizeof(value))) return false;
    }
    return true;
#else
    SavedState state {};
    state.params = self(plugin)->params;
    return stream && stream->write && writeFull(stream, &state, sizeof(state));
#endif
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
#if S3G_HAS_RNBO_EXPORT
    auto* p = self(plugin);
    StateHeader header {};
    if (!stream || !stream->read || !readFull(stream, &header, sizeof(header)) || header.version != kStateVersion) return false;
    const uint32_t n = std::min<uint32_t>(header.count, static_cast<uint32_t>(p->rnboParams.size()));
    for (uint32_t i = 0; i < header.count; ++i) {
        double value = 0.0;
        if (!readFull(stream, &value, sizeof(value))) return false;
        if (i < n) p->processor.rnbo.setParameterValue(p->rnboParams[i].index, value);
    }
#if defined(__APPLE__)
    if (p->guiView) [static_cast<NSView*>(p->guiView) setNeedsDisplay:YES];
#endif
    return true;
#else
    SavedState state {};
    if (!stream || !stream->read || !readFull(stream, &state, sizeof(state)) || state.version != kStateVersion) return false;
    self(plugin)->params = state.params;
    return true;
#endif
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

NSColor* uiColor(int rgb, double alpha = 1.0)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:alpha];
}

constexpr uint32_t kGuiWidth = 980;
constexpr uint32_t kGuiFallbackHeight = 320;
constexpr uint32_t kGuiPagedHeight = 520;
constexpr size_t kGuiParamsPerPage = 24;
constexpr size_t kGuiParamColumns = 4;
constexpr CGFloat kGuiParamStartY = 124;
constexpr CGFloat kGuiParamRowH = 42;
constexpr CGFloat kGuiPanelX = 24;
constexpr CGFloat kGuiPanelW = 932;
constexpr CGFloat kGuiStartX = 42;
constexpr CGFloat kGuiColW = 214;
constexpr CGFloat kGuiColStep = 226;
constexpr CGFloat kGuiTabW = 92;
constexpr CGFloat kGuiTabH = 22;
constexpr CGFloat kGuiTabGap = 6;
constexpr CGFloat kGuiTabRowGap = 4;
constexpr CGFloat kGuiEnumRowH = 20;

size_t guiTabColumns()
{
    const CGFloat available = (kGuiPanelX + kGuiPanelW) - kGuiStartX - 14;
    return std::max<size_t>(1, static_cast<size_t>((available + kGuiTabGap) / (kGuiTabW + kGuiTabGap)));
}

size_t guiTabRows(size_t pageCount)
{
    const size_t cols = guiTabColumns();
    return std::max<size_t>(1, (pageCount + cols - 1) / cols);
}

CGFloat guiParamStartY(size_t pageCount)
{
    return kGuiParamStartY + static_cast<CGFloat>(guiTabRows(pageCount) - 1) * (kGuiTabH + kGuiTabRowGap);
}

#if S3G_HAS_RNBO_EXPORT
struct GuiParamPage {
    std::string label;
    std::vector<size_t> indices;
};

bool splitChannelParamName(const std::string& name, std::string& channel, std::string& group)
{
    if (name.size() < 6) return false;
    if (name[0] != 'c' || name[1] != 'h') return false;
    if (!std::isdigit(static_cast<unsigned char>(name[2])) ||
        !std::isdigit(static_cast<unsigned char>(name[3])) ||
        name[4] != '_') return false;
    channel = name.substr(0, 4);
    group = name.substr(5);
    return !group.empty();
}

std::string groupNameForParam(const RnboParam& param)
{
    std::string channel;
    std::string group;
    if (splitChannelParamName(param.name, channel, group)) return group;
    const auto slash = param.name.find(static_cast<char>(47));
    if (slash != std::string::npos && slash > 0) return param.name.substr(0, slash);
    return param.name.empty() ? "params" : param.name;
}

std::string displayNameForParam(const RnboParam& param)
{
    std::string channel;
    std::string group;
    if (splitChannelParamName(param.name, channel, group)) return channel;
    const auto slash = param.name.find(static_cast<char>(47));
    if (slash != std::string::npos && slash + 1 < param.name.size()) return param.name.substr(slash + 1);
    return param.name;
}

int channelNumberForParam(const RnboParam& param)
{
    std::string channel;
    std::string group;
    if (!splitChannelParamName(param.name, channel, group)) return -1;
    return std::stoi(channel.substr(2));
}

std::vector<GuiParamPage> buildGuiParamPages(const Plugin* p)
{
    std::vector<GuiParamPage> pages;
    if (!p) return pages;
    for (size_t i = 0; i < p->rnboParams.size(); ++i) {
        const std::string group = groupNameForParam(p->rnboParams[i]);
        auto found = std::find_if(pages.begin(), pages.end(), [&](const GuiParamPage& page) {
            return page.label == group;
        });
        if (found == pages.end()) {
            pages.push_back(GuiParamPage { group, {} });
            found = pages.end() - 1;
        }
        found->indices.push_back(i);
    }
    for (auto& page : pages) {
        std::stable_sort(page.indices.begin(), page.indices.end(), [&](size_t a, size_t b) {
            const int channelA = channelNumberForParam(p->rnboParams[a]);
            const int channelB = channelNumberForParam(p->rnboParams[b]);
            if (channelA >= 0 && channelB >= 0 && channelA != channelB) return channelA < channelB;
            return p->rnboParams[a].name < p->rnboParams[b].name;
        });
    }
    if (pages.empty()) pages.push_back(GuiParamPage { "params", {} });
    return pages;
}
#endif

uint32_t preferredGuiHeight(const Plugin* p)
{
#if S3G_HAS_RNBO_EXPORT
    const auto pages = buildGuiParamPages(p);
    const size_t maxItems = pages.empty() ? 0 : std::max_element(pages.begin(), pages.end(), [](const GuiParamPage& a, const GuiParamPage& b) {
        return a.indices.size() < b.indices.size();
    })->indices.size();
    if (pages.size() > 1 || maxItems > kGuiParamsPerPage) return kGuiPagedHeight;
    const size_t rows = (maxItems + kGuiParamColumns - 1) / kGuiParamColumns;
    const auto height = static_cast<uint32_t>(guiParamStartY(pages.size()) + rows * kGuiParamRowH + 104);
    return std::max(kGuiFallbackHeight, height);
#else
    (void)p;
    return kGuiFallbackHeight;
#endif
}

@interface S3GRnboTestView : NSView {
@private
    void* _plugin;
    int _drag;
    int _openEnum;
    size_t _page;
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)stopTimer;
@end

@implementation S3GRnboTestView
- (id)initWithPlugin:(void*)plugin
{
    if ((self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, preferredGuiHeight(static_cast<Plugin*>(plugin)))])) {
        _plugin = plugin;
        _drag = -1;
        _openEnum = -1;
        _page = 0;
        _timer = [[NSTimer scheduledTimerWithTimeInterval:(1.0 / 30.0) target:self selector:@selector(tick:) userInfo:nil repeats:YES] retain];
    }
    return self;
}
- (void)dealloc
{
    [self stopTimer];
    [super dealloc];
}
- (void)stopTimer
{
    if (_timer) {
        [_timer invalidate];
        [_timer release];
        _timer = nil;
    }
}
- (void)tick:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}
- (BOOL)isFlipped { return YES; }
- (void)drawSlider:(NSString*)name value:(double)value y:(CGFloat)y attrs:(NSDictionary*)attrs dim:(NSDictionary*)dim
{
    [name drawAtPoint:NSMakePoint(42, y - 2) withAttributes:dim];
    NSRect track = NSMakeRect(126, y, 260, 10);
    [uiColor(0x111111) setFill];
    NSRectFill(track);
    [uiColor(0x626262) setStroke];
    NSFrameRect(track);
    NSRect fill = NSInsetRect(track, 1, 1);
    fill.size.width *= static_cast<CGFloat>(std::clamp(value, 0.0, 1.0));
    [uiColor(0xa8a8a8) setFill];
    NSRectFill(fill);
    [uiColor(0xe8e8e8) setFill];
    NSRectFill(NSMakeRect(track.origin.x + track.size.width * value - 1.5, track.origin.y - 2, 3, 14));
    [[NSString stringWithFormat:@"%.3f", value] drawAtPoint:NSMakePoint(410, y - 2) withAttributes:attrs];
}
- (void)drawCompactSlider:(NSString*)name value:(double)value frame:(NSRect)frame attrs:(NSDictionary*)attrs dim:(NSDictionary*)dim
{
    [name drawAtPoint:NSMakePoint(frame.origin.x, frame.origin.y) withAttributes:dim];
    NSRect track = NSMakeRect(frame.origin.x, frame.origin.y + 18, frame.size.width - 54, 8);
    [uiColor(0x111111) setFill];
    NSRectFill(track);
    [uiColor(0x626262) setStroke];
    NSFrameRect(track);
    NSRect fill = NSInsetRect(track, 1, 1);
    fill.size.width *= static_cast<CGFloat>(std::clamp(value, 0.0, 1.0));
    [uiColor(0xa8a8a8) setFill];
    NSRectFill(fill);
    [uiColor(0xe8e8e8) setFill];
    NSRectFill(NSMakeRect(track.origin.x + track.size.width * value - 1.5, track.origin.y - 2, 3, 12));
    [[NSString stringWithFormat:@"%.3f", value] drawAtPoint:NSMakePoint(frame.origin.x + frame.size.width - 45, frame.origin.y + 13) withAttributes:attrs];
}
#if S3G_HAS_RNBO_EXPORT
- (void)drawCompactEnum:(NSString*)name param:(const RnboParam&)param value:(double)value frame:(NSRect)frame attrs:(NSDictionary*)attrs dim:(NSDictionary*)dim
{
    [name drawAtPoint:NSMakePoint(frame.origin.x, frame.origin.y) withAttributes:dim];
    NSRect box = NSMakeRect(frame.origin.x, frame.origin.y + 16, frame.size.width, 18);
    [uiColor(0x151515) setFill];
    NSRectFill(box);
    [uiColor(0x626262) setStroke];
    NSFrameRect(box);
    const int count = static_cast<int>(param.enumValues.size());
    const int index = std::clamp(static_cast<int>(std::lround(value - param.min)), 0, std::max(0, count - 1));
    if (count > 0) {
        NSString* label = [NSString stringWithUTF8String:param.enumValues[static_cast<size_t>(index)].c_str()];
        [label drawAtPoint:NSMakePoint(box.origin.x + 6, box.origin.y + 3) withAttributes:attrs];
        [@"v" drawAtPoint:NSMakePoint(box.origin.x + box.size.width - 15, box.origin.y + 3) withAttributes:dim];
    }
}
- (double)valueForPoint:(NSPoint)pt frame:(NSRect)frame
{
    NSRect track = NSMakeRect(frame.origin.x, frame.origin.y + 18, frame.size.width - 54, 8);
    return std::clamp((pt.x - track.origin.x) / track.size.width, 0.0, 1.0);
}
- (double)enumValueForPoint:(NSPoint)pt frame:(NSRect)frame param:(const RnboParam&)param
{
    if (!isEnumParam(param)) return param.min;
    NSRect box = NSMakeRect(frame.origin.x, frame.origin.y + 16, frame.size.width, 16);
    const double normalized = std::clamp((pt.x - box.origin.x) / box.size.width, 0.0, 0.999999);
    const int index = std::clamp(static_cast<int>(normalized * static_cast<double>(param.enumValues.size())), 0, static_cast<int>(param.enumValues.size()) - 1);
    return param.min + static_cast<double>(index);
}
#endif
- (size_t)pageCount
{
#if S3G_HAS_RNBO_EXPORT
    auto* p = static_cast<Plugin*>(_plugin);
    return buildGuiParamPages(p).size();
#else
    return 1;
#endif
}
- (NSRect)tabRect:(size_t)page
{
    const size_t cols = guiTabColumns();
    const size_t col = page % cols;
    const size_t row = page / cols;
    return NSMakeRect(kGuiStartX + col * (kGuiTabW + kGuiTabGap), 78 + row * (kGuiTabH + kGuiTabRowGap), kGuiTabW, kGuiTabH);
}
- (NSRect)randomButtonRect
{
    return NSMakeRect(kGuiWidth - 318, 13, 62, 22);
}
- (NSRect)randomAmountRect
{
    return NSMakeRect(kGuiWidth - 246, 13, 118, 22);
}
- (NSRect)midiActivityRect
{
    return NSMakeRect(kGuiWidth - 120, 13, 26, 22);
}
- (CGFloat)currentParamStartY
{
    return guiParamStartY([self pageCount]);
}
- (NSRect)enumBoxForFrame:(NSRect)frame
{
    return NSMakeRect(frame.origin.x, frame.origin.y + 16, frame.size.width, 18);
}
#if S3G_HAS_RNBO_EXPORT
- (NSRect)enumMenuRectForParam:(const RnboParam&)param frame:(NSRect)frame
{
    const CGFloat menuH = std::max<CGFloat>(kGuiEnumRowH, static_cast<CGFloat>(param.enumValues.size()) * kGuiEnumRowH);
    NSRect box = [self enumBoxForFrame:frame];
    CGFloat y = box.origin.y + box.size.height + 2;
    if (y + menuH > self.bounds.size.height - 8) y = box.origin.y - menuH - 2;
    return NSMakeRect(box.origin.x, y, box.size.width, menuH);
}
#endif
- (void)drawButton:(NSString*)label frame:(NSRect)frame attrs:(NSDictionary*)attrs
{
    [uiColor(0x181818) setFill];
    NSRectFill(frame);
    [uiColor(0x626262) setStroke];
    NSFrameRect(frame);
    [label drawAtPoint:NSMakePoint(frame.origin.x + 10, frame.origin.y + 5) withAttributes:attrs];
}
- (void)drawRandomAmountWithAttrs:(NSDictionary*)attrs dim:(NSDictionary*)dim
{
    auto* p = static_cast<Plugin*>(_plugin);
    NSRect frame = [self randomAmountRect];
    [@"DEV" drawAtPoint:NSMakePoint(frame.origin.x, frame.origin.y + 5) withAttributes:dim];
    NSRect track = NSMakeRect(frame.origin.x + 30, frame.origin.y + 8, frame.size.width - 62, 8);
    const double value = std::clamp(static_cast<double>(p->randomAmount.load(std::memory_order_relaxed)), 0.0, 1.0);
    [uiColor(0x111111) setFill];
    NSRectFill(track);
    [uiColor(0x626262) setStroke];
    NSFrameRect(track);
    NSRect fill = NSInsetRect(track, 1, 1);
    fill.size.width *= static_cast<CGFloat>(value);
    [uiColor(0xa8a8a8) setFill];
    NSRectFill(fill);
    [uiColor(0xe8e8e8) setFill];
    NSRectFill(NSMakeRect(track.origin.x + track.size.width * value - 1.5, track.origin.y - 2, 3, 12));
    [[NSString stringWithFormat:@"%.2f", value] drawAtPoint:NSMakePoint(frame.origin.x + frame.size.width - 28, frame.origin.y + 5) withAttributes:attrs];
}
- (void)drawMidiActivityWithAttrs:(NSDictionary*)attrs dim:(NSDictionary*)dim
{
#if S3G_HAS_RNBO_EXPORT
    auto* p = static_cast<Plugin*>(_plugin);
    if (p->processor.rnbo.getNumMidiInputPorts() <= 0) {
        (void)attrs;
        (void)dim;
        return;
    }
    NSRect frame = [self midiActivityRect];
    const double activity = std::clamp(static_cast<double>(p->midiActivity.load(std::memory_order_relaxed)), 0.0, 1.0);
    [uiColor(activity > 0.05 ? 0xd1d1d1 : 0x181818) setFill];
    NSRectFill(frame);
    [uiColor(activity > 0.05 ? 0xf0f0f0 : 0x626262) setStroke];
    NSFrameRect(frame);
    [@"M" drawAtPoint:NSMakePoint(frame.origin.x + 9, frame.origin.y + 5) withAttributes:activity > 0.05 ? @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular], NSForegroundColorAttributeName:uiColor(0x0c0c0c) } : dim];
#else
    (void)attrs;
    (void)dim;
#endif
}
- (void)setRandomAmountFromPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    NSRect frame = [self randomAmountRect];
    NSRect track = NSMakeRect(frame.origin.x + 30, frame.origin.y + 8, frame.size.width - 62, 8);
    const double value = std::clamp((pt.x - track.origin.x) / track.size.width, 0.0, 1.0);
    p->randomAmount.store(static_cast<float>(value), std::memory_order_relaxed);
    [self setNeedsDisplay:YES];
}
- (void)drawOpenEnumMenuWithAttrs:(NSDictionary*)attrs dim:(NSDictionary*)dim
{
#if S3G_HAS_RNBO_EXPORT
    if (_openEnum < 0) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const auto pages = buildGuiParamPages(p);
    if (_page >= pages.size() || static_cast<size_t>(_openEnum) >= p->rnboParams.size()) return;
    const auto& pageIndices = pages[_page].indices;
    const auto found = std::find(pageIndices.begin(), pageIndices.end(), static_cast<size_t>(_openEnum));
    if (found == pageIndices.end()) return;
    const size_t localIndex = static_cast<size_t>(std::distance(pageIndices.begin(), found));
    const size_t col = localIndex % kGuiParamColumns;
    const size_t row = localIndex / kGuiParamColumns;
    NSRect frame = NSMakeRect(kGuiStartX + col * kGuiColStep, [self currentParamStartY] + row * kGuiParamRowH, kGuiColW, 34);
    const auto& param = p->rnboParams[static_cast<size_t>(_openEnum)];
    if (!isEnumParam(param)) return;
    NSRect menu = [self enumMenuRectForParam:param frame:frame];
    [uiColor(0x101010) setFill];
    NSRectFill(menu);
    [uiColor(0xd1d1d1) setStroke];
    NSFrameRect(menu);
    const double value = p->processor.rnbo.getParameterValue(param.index);
    const int selected = std::clamp(static_cast<int>(std::lround(value - param.min)), 0, static_cast<int>(param.enumValues.size()) - 1);
    for (size_t i = 0; i < param.enumValues.size(); ++i) {
        NSRect item = NSMakeRect(menu.origin.x, menu.origin.y + static_cast<CGFloat>(i) * kGuiEnumRowH, menu.size.width, kGuiEnumRowH);
        if (static_cast<int>(i) == selected) {
            [uiColor(0x3a3a3a) setFill];
            NSRectFill(NSInsetRect(item, 1, 1));
        }
        NSString* label = [NSString stringWithUTF8String:param.enumValues[i].c_str()];
        [label drawAtPoint:NSMakePoint(item.origin.x + 6, item.origin.y + 4) withAttributes:static_cast<int>(i) == selected ? attrs : dim];
    }
#else
    (void)attrs;
    (void)dim;
#endif
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    [uiColor(0x0c0c0c) setFill];
    NSRectFill(self.bounds);
    NSDictionary* text = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular], NSForegroundColorAttributeName:uiColor(0xf0f0f0) };
    NSDictionary* dim = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular], NSForegroundColorAttributeName:uiColor(0x9e9e9e) };
    [[NSString stringWithUTF8String:S3G_RNBO_PLUGIN_NAME] drawAtPoint:NSMakePoint(18, 16) withAttributes:text];
    [self drawButton:@"RAND" frame:[self randomButtonRect] attrs:text];
    [self drawRandomAmountWithAttrs:text dim:dim];
    [self drawMidiActivityWithAttrs:text dim:dim];
    [[NSString stringWithFormat:@"PK %.3f", p->peak.load(std::memory_order_relaxed)] drawAtPoint:NSMakePoint(kGuiWidth - 82, 16) withAttributes:dim];
#if S3G_HAS_RNBO_EXPORT
    const auto pages = buildGuiParamPages(p);
    const size_t totalPages = pages.size();
    if (_page >= totalPages) _page = totalPages - 1;
    const auto& pageIndices = pages[_page].indices;
    const size_t pageItems = pageIndices.size();
    const CGFloat rows = static_cast<CGFloat>((pageItems + kGuiParamColumns - 1) / kGuiParamColumns);
    const CGFloat startY = guiParamStartY(totalPages);
    const CGFloat panelHeight = std::max<CGFloat>(210, startY + rows * kGuiParamRowH + 72 - 48);
#else
    const CGFloat panelHeight = 210;
#endif
    [uiColor(0x1d1d1d) setFill];
    NSRectFill(NSMakeRect(kGuiPanelX, 48, kGuiPanelW, panelHeight));
    [uiColor(0x636363) setStroke];
    NSFrameRect(NSMakeRect(kGuiPanelX, 48, kGuiPanelW, panelHeight));
    [uiColor(0x131313) setFill];
    NSRectFill(NSMakeRect(kGuiPanelX, 48, kGuiPanelW, 22));
    [uiColor(0xd1d1d1) setFill];
    NSRectFill(NSMakeRect(kGuiPanelX, 48, kGuiPanelW, 2));
    [@"ENGINE" drawAtPoint:NSMakePoint(kGuiStartX, 54) withAttributes:text];
#if S3G_HAS_RNBO_EXPORT
    for (size_t page = 0; page < totalPages; ++page) {
        NSRect tab = [self tabRect:page];
        [uiColor(page == _page ? 0x3a3a3a : 0x181818) setFill];
        NSRectFill(tab);
        [uiColor(page == _page ? 0xd1d1d1 : 0x626262) setStroke];
        NSFrameRect(tab);
        NSString* label = [NSString stringWithUTF8String:pages[page].label.c_str()];
        [label drawAtPoint:NSMakePoint(tab.origin.x + 8, tab.origin.y + 5) withAttributes:page == _page ? text : dim];
    }
    const CGFloat colW = kGuiColW;
    const CGFloat startX = kGuiStartX;
    const CGFloat controlsBottom = startY + rows * kGuiParamRowH;
    for (size_t localIndex = 0; localIndex < pageIndices.size(); ++localIndex) {
        const size_t col = localIndex % kGuiParamColumns;
        const size_t row = localIndex / kGuiParamColumns;
        NSRect frame = NSMakeRect(startX + col * kGuiColStep, startY + row * kGuiParamRowH, colW, 34);
        const auto& param = p->rnboParams[pageIndices[localIndex]];
        const double raw = p->processor.rnbo.getParameterValue(param.index);
        const double span = param.max - param.min;
        const double normalized = span > 0.0 ? (raw - param.min) / span : 0.0;
        const std::string displayName = displayNameForParam(param);
        NSString* name = [NSString stringWithUTF8String:displayName.c_str()];
        if (isEnumParam(param)) [self drawCompactEnum:name param:param value:raw frame:frame attrs:text dim:dim];
        else [self drawCompactSlider:name value:normalized frame:frame attrs:text dim:dim];
    }
#else
    [self drawSlider:@"GAIN" value:p->params.gain y:94 attrs:text dim:dim];
    [self drawSlider:@"MIX" value:p->params.mix y:128 attrs:text dim:dim];
    [self drawSlider:@"OUT" value:p->params.output y:162 attrs:text dim:dim];
#endif
#if S3G_HAS_RNBO_EXPORT
    NSString* mode = @"RNBO EXPORT: ON";
#else
    NSString* mode = @"RNBO EXPORT: FALLBACK DSP";
#endif
#if S3G_HAS_RNBO_EXPORT
    const CGFloat metaY = controlsBottom + 18;
    [mode drawAtPoint:NSMakePoint(kGuiStartX, metaY) withAttributes:dim];
    [[NSString stringWithFormat:@"IO %u IN / %u OUT", kInputChannels, kOutputChannels] drawAtPoint:NSMakePoint(kGuiWidth - 210, metaY) withAttributes:dim];
    [[NSString stringWithFormat:@"PARAMS %zu  PAGE %zu/%zu  GROUP %s", p->rnboParams.size(), _page + 1, totalPages, pages[_page].label.c_str()] drawAtPoint:NSMakePoint(kGuiStartX, metaY + 18) withAttributes:dim];
    [self drawOpenEnumMenuWithAttrs:text dim:dim];
#else
    [mode drawAtPoint:NSMakePoint(42, 214) withAttributes:dim];
    [[NSString stringWithFormat:@"IO %u IN / %u OUT", kInputChannels, kOutputChannels] drawAtPoint:NSMakePoint(410, 214) withAttributes:dim];
#endif
}
- (void)setParamFromPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
#if S3G_HAS_RNBO_EXPORT
    if (_drag >= 0 && static_cast<size_t>(_drag) < p->rnboParams.size()) {
        const auto pages = buildGuiParamPages(p);
        if (_page >= pages.size()) return;
        const auto& pageIndices = pages[_page].indices;
        const auto found = std::find(pageIndices.begin(), pageIndices.end(), static_cast<size_t>(_drag));
        if (found == pageIndices.end()) return;
        const size_t localIndex = static_cast<size_t>(std::distance(pageIndices.begin(), found));
        const size_t col = localIndex % kGuiParamColumns;
        const size_t row = localIndex / kGuiParamColumns;
        NSRect frame = NSMakeRect(kGuiStartX + col * kGuiColStep, [self currentParamStartY] + row * kGuiParamRowH, kGuiColW, 34);
        const auto& param = p->rnboParams[static_cast<size_t>(_drag)];
        if (isEnumParam(param)) {
            (void)pt;
        } else {
            const double normalized = [self valueForPoint:pt frame:frame];
            setParam(*p, param.id, steppedParamValue(param, param.min + normalized * (param.max - param.min)));
        }
    }
#else
    const double n = std::clamp((pt.x - 126.0) / 260.0, 0.0, 1.0);
    if (_drag == 0) setParam(*p, kGain, n);
    else if (_drag == 1) setParam(*p, kMix, n);
    else if (_drag == 2) setParam(*p, kOutput, n);
#endif
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (NSPointInRect(pt, [self randomButtonRect])) {
        _drag = -1;
        _openEnum = -1;
        randomizeParams(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, [self randomAmountRect])) {
        _drag = -2;
        _openEnum = -1;
        [self setRandomAmountFromPoint:pt];
        return;
    }
#if S3G_HAS_RNBO_EXPORT
    const auto pages = buildGuiParamPages(p);
    const size_t totalPages = pages.size();
    if (_openEnum >= 0 && _page < pages.size() && static_cast<size_t>(_openEnum) < p->rnboParams.size()) {
        const auto& pageIndices = pages[_page].indices;
        const auto found = std::find(pageIndices.begin(), pageIndices.end(), static_cast<size_t>(_openEnum));
        if (found != pageIndices.end()) {
            const size_t localIndex = static_cast<size_t>(std::distance(pageIndices.begin(), found));
            const size_t col = localIndex % kGuiParamColumns;
            const size_t row = localIndex / kGuiParamColumns;
            NSRect frame = NSMakeRect(kGuiStartX + col * kGuiColStep, [self currentParamStartY] + row * kGuiParamRowH, kGuiColW, 34);
            const auto& param = p->rnboParams[static_cast<size_t>(_openEnum)];
            NSRect menu = [self enumMenuRectForParam:param frame:frame];
            if (NSPointInRect(pt, menu)) {
                const int item = std::clamp(static_cast<int>((pt.y - menu.origin.y) / kGuiEnumRowH), 0, static_cast<int>(param.enumValues.size()) - 1);
                setParam(*p, param.id, param.min + static_cast<double>(item));
                _openEnum = -1;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        _openEnum = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    for (size_t page = 0; page < totalPages; ++page) {
        if (NSPointInRect(pt, [self tabRect:page])) {
            _page = page;
            _drag = -1;
            _openEnum = -1;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_page >= pages.size()) return;
    const auto& pageIndices = pages[_page].indices;
    for (size_t localIndex = 0; localIndex < pageIndices.size(); ++localIndex) {
        const size_t col = localIndex % kGuiParamColumns;
        const size_t row = localIndex / kGuiParamColumns;
        NSRect frame = NSMakeRect(kGuiStartX + col * kGuiColStep, [self currentParamStartY] + row * kGuiParamRowH, kGuiColW, 34);
        if (NSPointInRect(pt, frame)) {
            const auto& param = p->rnboParams[pageIndices[localIndex]];
            if (isEnumParam(param) && NSPointInRect(pt, [self enumBoxForFrame:frame])) {
                _drag = -1;
                _openEnum = static_cast<int>(pageIndices[localIndex]);
                [self setNeedsDisplay:YES];
                return;
            }
            _drag = static_cast<int>(pageIndices[localIndex]);
            _openEnum = -1;
            [self setParamFromPoint:pt];
            return;
        }
    }
#else
    const CGFloat rows[] = { 94, 128, 162 };
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(pt, NSMakeRect(38, rows[i] - 8, 450, 26))) {
            _drag = i;
            [self setParamFromPoint:pt];
            return;
        }
    }
#endif
}
- (void)mouseDragged:(NSEvent*)event
{
    if (_drag == -2) [self setRandomAmountFromPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    else if (_drag >= 0) [self setParamFromPoint:[self convertPoint:event.locationInWindow fromView:nil]];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _drag = -1; }
- (void)scrollWheel:(NSEvent*)event
{
    (void)event;
}
@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (!p->guiView) p->guiView = [[S3GRnboTestView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { NSView* view = static_cast<NSView*>(p->guiView); if ([view respondsToSelector:@selector(stopTimer)]) [view performSelector:@selector(stopTimer)]; [view removeFromSuperview]; [view release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = preferredGuiHeight(self(plugin)); return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* view = static_cast<NSView*>(p->guiView); [static_cast<NSView*>(win->cocoa) addSubview:view]; [view setFrame:NSMakeRect(0, 0, kGuiWidth, preferredGuiHeight(p))]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setHidden:NO]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }

const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
#if S3G_RNBO_INPUT_CHANNELS == 0
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
#else
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
#endif
    kOutputChannels == 2 ? CLAP_PLUGIN_FEATURE_STEREO : CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    S3G_RNBO_PLUGIN_ID,
    S3G_RNBO_PLUGIN_NAME,
    "s3g",
    "https://github.com/s3g/s3g-rnbo-clap",
    "",
    "",
    "0.1.0",
    S3G_RNBO_PLUGIN_DESCRIPTION,
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
#if S3G_HAS_RNBO_EXPORT
    discoverRnboParams(*p);
#endif
    p->plugin.desc = &descriptor;
    p->plugin.plugin_data = p;
    p->plugin.init = init;
    p->plugin.destroy = destroy;
    p->plugin.activate = activate;
    p->plugin.deactivate = deactivate;
    p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing;
    p->plugin.reset = reset;
    p->plugin.process = process;
    p->plugin.get_extension = getExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
