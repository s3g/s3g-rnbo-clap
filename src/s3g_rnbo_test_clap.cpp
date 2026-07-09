#include "s3g_rnbo_fallback.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kInputChannels = S3G_RNBO_INPUT_CHANNELS;
constexpr uint32_t kOutputChannels = S3G_RNBO_OUTPUT_CHANNELS;
constexpr uint32_t kStateVersion = 1;

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

struct RnboProcessor {
    double sampleRate = 48000.0;
    uint32_t maxBlock = 1024;
    std::vector<float> fallbackIn;
    std::vector<float> fallbackOut;
    float gainSmooth = 2.0f / 3.0f;
    float mixSmooth = 1.0f;
    float outSmooth = 1.0f;
#if S3G_HAS_RNBO_EXPORT
    RNBO::CoreObject rnbo {};
    std::vector<RNBO::SampleValue> inputStorage;
    std::vector<RNBO::SampleValue> outputStorage;
    std::vector<RNBO::SampleValue*> inputPtrs;
    std::vector<RNBO::SampleValue*> outputPtrs;
#endif

    void prepare(double sr, uint32_t blockSize)
    {
        sampleRate = std::max(1.0, sr);
        maxBlock = std::max<uint32_t>(1u, blockSize);
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
#endif
    }

    void reset()
    {
        std::fill(fallbackIn.begin(), fallbackIn.end(), 0.0f);
        std::fill(fallbackOut.begin(), fallbackOut.end(), 0.0f);
#if S3G_HAS_RNBO_EXPORT
        rnbo.prepareToProcess(sampleRate, maxBlock);
#endif
    }

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
                     uint32_t frames,
                     const Params& params)
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

        rnbo.process(inputPtrs.data(), kInputChannels, outputPtrs.data(), kOutputChannels, n);

        gainSmooth = s3g::rnbo_lab::smoothParam(gainSmooth, params.gain);
        mixSmooth = s3g::rnbo_lab::smoothParam(mixSmooth, params.mix);
        outSmooth = s3g::rnbo_lab::smoothParam(outSmooth, params.output);
        const float gain = std::pow(10.0f, (gainSmooth * 36.0f - 24.0f) / 20.0f);
        for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
            for (uint32_t i = 0; i < n; ++i) {
                const float dry = ch < input.channel_count && input.data32 && input.data32[ch] ? input.data32[ch][i] : 0.0f;
                const float wet = ch < kOutputChannels ? static_cast<float>(outputPtrs[ch][i]) * gain : 0.0f;
                const float value = (dry + (wet - dry) * s3g::rnbo_lab::clamp01(mixSmooth)) * outSmooth;
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
    std::atomic<float> peak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

float clampParam(clap_id id, double value)
{
    if (id == kGain || id == kMix || id == kOutput) return static_cast<float>(std::clamp(value, 0.0, 1.0));
    return static_cast<float>(value);
}

void setParam(Plugin& p, clap_id id, double value)
{
    const float v = clampParam(id, value);
    if (id == kGain) p.params.gain = v;
    else if (id == kMix) p.params.mix = v;
    else if (id == kOutput) p.params.output = v;
    else return;
#if defined(__APPLE__)
    if (p.guiView) [static_cast<NSView*>(p.guiView) setNeedsDisplay:YES];
#endif
}

double getParam(const Plugin& p, clap_id id)
{
    if (id == kGain) return p.params.gain;
    if (id == kMix) return p.params.mix;
    if (id == kOutput) return p.params.output;
    return 0.0;
}

void readEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            setParam(p, param->param_id, param->value);
        }
    }
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiView) {
        NSView* view = static_cast<NSView*>(p->guiView);
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
    readEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    float blockPeak = 0.0f;
#if S3G_HAS_RNBO_EXPORT
    p->processor.processRnbo(input, output, proc->frames_count, p->params);
    for (uint32_t ch = 0; ch < std::min(output.channel_count, kOutputChannels); ++ch) {
        if (!output.data32 || !output.data32[ch]) continue;
        for (uint32_t i = 0; i < proc->frames_count; ++i) blockPeak = std::max(blockPeak, std::fabs(output.data32[ch][i]));
    }
#else
    for (uint32_t i = 0; i < proc->frames_count; ++i) {
        p->processor.processFallback(input, output, i, p->params);
        for (uint32_t ch = 0; ch < std::min(output.channel_count, kOutputChannels); ++ch) {
            if (output.data32 && output.data32[ch]) blockPeak = std::max(blockPeak, std::fabs(output.data32[ch][i]));
        }
    }
#endif
    p->peak.store(std::max(blockPeak, p->peak.load(std::memory_order_relaxed) * 0.92f), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 1 : 2;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "RNBO In" : "RNBO Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = (info->channel_count == 2) ? CLAP_PORT_STEREO : CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

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

uint32_t paramsCount(const clap_plugin_t*) { return 3; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= 3) return false;
    if (index == 0) *info = makeParam(kGain, "Gain", 0.50);
    else if (index == 1) *info = makeParam(kMix, "Mix", 1.0);
    else *info = makeParam(kOutput, "Output", 1.0);
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    *value = getParam(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    std::snprintf(display, size, "%.3f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = clampParam(id, std::atof(display));
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
    SavedState state {};
    state.params = self(plugin)->params;
    return stream && stream->write && writeFull(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    SavedState state {};
    if (!stream || !stream->read || !readFull(stream, &state, sizeof(state)) || state.version != kStateVersion) return false;
    self(plugin)->params = state.params;
    return true;
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

@interface S3GRnboTestView : NSView {
@private
    void* _plugin;
    int _drag;
}
- (id)initWithPlugin:(void*)plugin;
@end

@implementation S3GRnboTestView
- (id)initWithPlugin:(void*)plugin
{
    if ((self = [super initWithFrame:NSMakeRect(0, 0, 620, 320)])) {
        _plugin = plugin;
        _drag = -1;
    }
    return self;
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
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    [uiColor(0x0c0c0c) setFill];
    NSRectFill(self.bounds);
    NSDictionary* text = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular], NSForegroundColorAttributeName:uiColor(0xf0f0f0) };
    NSDictionary* dim = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular], NSForegroundColorAttributeName:uiColor(0x9e9e9e) };
    [@"s3g RNBO 8ch Passthru" drawAtPoint:NSMakePoint(18, 16) withAttributes:text];
    [[NSString stringWithFormat:@"PK %.3f", p->peak.load(std::memory_order_relaxed)] drawAtPoint:NSMakePoint(520, 16) withAttributes:dim];
    [uiColor(0x1d1d1d) setFill];
    NSRectFill(NSMakeRect(24, 48, 572, 210));
    [uiColor(0x636363) setStroke];
    NSFrameRect(NSMakeRect(24, 48, 572, 210));
    [uiColor(0x131313) setFill];
    NSRectFill(NSMakeRect(24, 48, 572, 22));
    [uiColor(0xd1d1d1) setFill];
    NSRectFill(NSMakeRect(24, 48, 572, 2));
    [@"ENGINE" drawAtPoint:NSMakePoint(42, 54) withAttributes:text];
    [self drawSlider:@"GAIN" value:p->params.gain y:94 attrs:text dim:dim];
    [self drawSlider:@"MIX" value:p->params.mix y:128 attrs:text dim:dim];
    [self drawSlider:@"OUT" value:p->params.output y:162 attrs:text dim:dim];
#if S3G_HAS_RNBO_EXPORT
    NSString* mode = @"RNBO EXPORT: ON";
#else
    NSString* mode = @"RNBO EXPORT: FALLBACK DSP";
#endif
    [mode drawAtPoint:NSMakePoint(42, 214) withAttributes:dim];
    [[NSString stringWithFormat:@"IO %u IN / %u OUT", kInputChannels, kOutputChannels] drawAtPoint:NSMakePoint(410, 214) withAttributes:dim];
}
- (void)setParamFromPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    const double n = std::clamp((pt.x - 126.0) / 260.0, 0.0, 1.0);
    if (_drag == 0) setParam(*p, kGain, n);
    else if (_drag == 1) setParam(*p, kMix, n);
    else if (_drag == 2) setParam(*p, kOutput, n);
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    const CGFloat rows[] = { 94, 128, 162 };
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(pt, NSMakeRect(38, rows[i] - 8, 450, 26))) {
            _drag = i;
            [self setParamFromPoint:pt];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event { if (_drag >= 0) [self setParamFromPoint:[self convertPoint:event.locationInWindow fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _drag = -1; }
@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (!p->guiView) p->guiView = [[S3GRnboTestView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { NSView* view = static_cast<NSView*>(p->guiView); [view removeFromSuperview]; [view release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 620; *h = 320; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* view = static_cast<NSView*>(p->guiView); [static_cast<NSView*>(win->cocoa) addSubview:view]; [view setFrame:NSMakeRect(0, 0, 620, 320)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setHidden:NO]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }

const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    kOutputChannels == 2 ? CLAP_PLUGIN_FEATURE_STEREO : CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-rnbo-clap.8ch-passthru",
    "s3g RNBO 8ch Passthru",
    "s3g",
    "https://github.com/s3g/s3g-rnbo-clap",
    "",
    "",
    "0.1.0",
    "Eight-channel CLAP wrapper test for an RNBO C++ passthrough export.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
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
