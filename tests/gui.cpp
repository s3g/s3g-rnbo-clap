#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif
#include "../src/s3g_rnbo_test_clap.cpp"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
namespace {
bool passed = true;
void expect(bool value, const char* name)
{
    if (!value) {
        passed = false;
        std::cerr << name << '\n';
    }
}
struct Output {
    std::map<clap_id, int> depth;
    bool balanced = true;
    unsigned count = 0, limit = 1000000;
    clap_output_events_t events { this,
        [](const clap_output_events_t* out, const clap_event_header_t* h) {
            auto& self = *static_cast<Output*>(out->ctx);
            if (!self.limit)
                return false;
            --self.limit;
            ++self.count;
            if (h->type == CLAP_EVENT_PARAM_VALUE) {
                auto id = reinterpret_cast<const clap_event_param_value_t*>(h)->param_id;
                if (self.depth[id] != 1)
                    self.balanced = false;
            } else {
                auto id = reinterpret_cast<const clap_event_param_gesture_t*>(h)->param_id;
                self.depth[id] += h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? 1 : -1;
                if (self.depth[id] < 0 || self.depth[id] > 1)
                    self.balanced = false;
            }
            return true;
        } };
    bool complete() const
    {
        if (!balanced)
            return false;
        for (auto [id, n] : depth)
            if (n)
                return false;
        return true;
    }
};
void render(rnbo_gui::Editor& editor, const std::string& suffix)
{
    auto context = VSTGUI::COffscreenContext::create({ 980., double(editor.p.nativeGuiHeight) });
    expect(bool(context), "offscreen renderer");
    if (!context)
        return;
    context->beginDraw();
    editor.draw(context);
    context->endDraw();
    if (const char* root = std::getenv("S3G_RNBO_CAPTURE_DIR")) {
        auto directory = std::filesystem::u8path(root);
        std::filesystem::create_directories(directory);
        auto bytes = VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(
            context->getBitmap()->getPlatformBitmap());
        std::ofstream file(directory / (std::string(S3G_RNBO_PLUGIN_ID) + "-" + suffix + ".png"),
            std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        expect(bool(file), "reference screenshot");
    }
}
void click(rnbo_gui::Editor& editor, double x, double y, unsigned clicks = 1)
{
    VSTGUI::MouseDownEvent down;
    down.mousePosition = { x, y };
    down.buttonState = VSTGUI::MouseButton::Left;
    down.clickCount = clicks;
    editor.onMouseDownEvent(down);
    VSTGUI::MouseUpEvent up;
    up.mousePosition = { x, y };
    editor.onMouseUpEvent(up);
}
void nativeWindow(const clap_plugin_t* plugin)
{
    const auto* gui
        = static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin, CLAP_EXT_GUI));
    expect(gui != nullptr, "CLAP GUI extension");
    if (!gui)
        return;
    const char* api = nullptr;
    bool floating = true;
    expect(gui->get_preferred_api(plugin, &api, &floating) && !floating, "embedded platform API");
    clap_gui_resize_hints_t hints {};
    expect(gui->get_resize_hints(plugin, &hints) && hints.preserve_aspect_ratio,
        "proportional resize hints");
    const auto nativeHeight = self(plugin)->nativeGuiHeight;
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        expect(gui->create(plugin, api, false), "create and reopen GUI");
#if defined(__APPLE__)
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 980, nativeHeight)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [window setReleasedWhenClosed:NO];
        clap_window_t parent {};
        parent.api = CLAP_WINDOW_API_COCOA;
        parent.cocoa = [window contentView];
        expect(gui->set_parent(plugin, &parent), "native window attachment");
        expect(gui->show(plugin), "native GUI show");
        [window orderFront:nil];
#endif
        for (double scale : { .1, .65, 1., 1.5, 2., 3. }) {
            uint32_t w = uint32_t(std::lround(980. * scale));
            uint32_t h = uint32_t(std::lround(nativeHeight * scale));
            const double bounded = std::clamp(scale, .65, 2.);
            expect(gui->adjust_size(plugin, &w, &h), "adjust size");
            expect(std::abs(double(w) - std::lround(980. * bounded)) <= 1.
                    && std::abs(double(h) - std::lround(nativeHeight * bounded)) <= 1.,
                "65-200 percent resize bounds");
            expect(gui->set_size(plugin, w, h), "apply proportional size");
            uint32_t actualW = 0, actualH = 0;
            expect(gui->get_size(plugin, &actualW, &actualH) && actualW == w && actualH == h,
                "resized GUI dimensions");
#if defined(__APPLE__)
            [window setContentSize:NSMakeSize(w, h)];
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
#endif
        }
        gui->hide(plugin);
        gui->destroy(plugin);
#if defined(__APPLE__)
        [window close];
        [window release];
#endif
    }
}
void sourceImport(Plugin& p)
{
#if S3G_HAS_RNBO_EXPORT
    if (!p.processor.hasExternalDataRef("src"))
        return;
    namespace fs = std::filesystem;
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = fs::temp_directory_path() / ("s3g-rnbo-src-" + std::to_string(token));
    if (!fs::create_directory(directory)) {
        expect(false, "source fixture directory");
        return;
    }
    const auto filename = directory / fs::u8path("音声 café.wav");
    const unsigned channels = kOutputChannels, frames = 4096, bytes = channels * frames * 2;
    {
        std::ofstream file(filename, std::ios::binary);
        auto u16 = [&](unsigned value) {
            file.put(char(value & 255));
            file.put(char((value >> 8) & 255));
        };
        auto u32 = [&](unsigned value) {
            u16(value & 65535);
            u16(value >> 16);
        };
        file.write("RIFF", 4);
        u32(36 + bytes);
        file.write("WAVEfmt ", 8);
        u32(16);
        u16(1);
        u16(channels);
        u32(48000);
        u32(48000 * channels * 2);
        u16(channels * 2);
        u16(16);
        file.write("data", 4);
        u32(bytes);
        for (unsigned i = 0; i < frames; ++i)
            for (unsigned ch = 0; ch < channels; ++ch)
                u16(500 + ch);
        expect(bool(file), "source fixture write");
    }
    const bool loaded = rnbo_gui::loadSource(p, filename.u8string());
    expect(loaded && p.processor.sourcePath == filename.u8string(), "RNBO src Unicode import");
    if (loaded) {
        auto* samples = p.processor.externalAudioBuffers.back()->data.get();
        expect(std::abs(samples[0] - 500.f / 32768.f) < 1.e-6, "decoded samples handed to RNBO");
        expect(!rnbo_gui::loadSource(p, (directory / "missing.wav").u8string())
                && p.processor.sourcePath == filename.u8string()
                && p.processor.externalAudioBuffers.back()->data.get() == samples,
            "failed RNBO import preserves prior source path and samples");
    }
    fs::remove(filename);
    fs::remove(directory);
#else
    (void)p;
#endif
}
void run(rnbo_gui::Editor& editor, Plugin& p)
{
    Output output;
    sourceImport(p);
    render(editor, "first-open");
    expect(rnbo_gui::guiPluginTitle().rfind("s3g RNBO", 0) == 0, "GUI-only title casing");
    expect(s3g::portable_gui::foundation::usingBundledFont(),
        "Fira Code loaded from bundled resources");
#if S3G_HAS_RNBO_EXPORT
    size_t total = 0;
    for (size_t page = 0; page < editor.pages.size(); ++page) {
        editor.page = page;
        const auto& group = editor.pages[page];
        total += group.indices.size();
        expect(group.indices.size() <= 24, "all parameter groups bounded to 24 controls");
        const auto bottom = rnbo_gui::paramTop(editor.pages.size())
            + ((group.indices.size() + 3) / 4) * 42. + 72.;
        expect(bottom < p.nativeGuiHeight, "all controls/status fit inside native height");
        render(editor, "page-" + std::to_string(page));
        for (size_t local = 0; local < group.indices.size(); ++local) {
            const auto& param = p.rnboParams[group.indices[local]];
            const double x = 40. + (local % 4) * 226.,
                         y = rnbo_gui::paramTop(editor.pages.size()) + (local / 4) * 42.;
            if (isEnumParam(param)) {
                click(editor, x + 50., y + 24.);
                render(editor, "enum-menu");
                // The shared menu chooses its vertical side to stay on-canvas.
                VSTGUI::MouseDownEvent dismiss;
                dismiss.mousePosition = { 2, 2 };
                dismiss.buttonState = VSTGUI::MouseButton::Left;
                editor.onMouseDownEvent(dismiss);
                expect(editor.perform(param.id, param.min + param.enumValues.size() - 1),
                    "enum automation");
            } else {
                editor.perform(param.id, param.max);
                click(editor, x + 10., y + 8., 2);
                expect(std::abs(getParam(p, param.id) - param.defaultValue) < 1.e-6,
                    "double-click resets RNBO default");
            }
            paramsFlush(&p.plugin, nullptr, &output.events);
        }
    }
    expect(
        total == p.rnboParams.size(), "every reflected RNBO parameter is accessible exactly once");
    expect(!rnbo_gui::loadSource(p, "/missing-rnbo-audio.wav"), "failed audio load is safe");
#else
    click(editor, 300, 98);
    paramsFlush(&p.plugin, nullptr, &output.events);
    expect(std::abs(getParam(p, kGain) - (300. - 126.) / 260.) < 1.e-6,
        "fallback slider pointer mapping");
    click(editor, 300, 98, 2);
    paramsFlush(&p.plugin, nullptr, &output.events);
    expect(std::abs(getParam(p, kGain) - Params {}.gain) < 1.e-6, "fallback default reset");
#endif
    editor.randomize();
    paramsFlush(&p.plugin, nullptr, &output.events);
    const bool hasParameters = paramsCount(&p.plugin) > 0;
    expect((!hasParameters || output.count > 0) && output.complete(),
        "balanced automation for edits and RAND");
    clap_param_info_t info {};
    if (hasParameters) {
        paramsGetInfo(&p.plugin, 0, &info);
        const auto original = getParam(p, info.id);
        editor.beginEdit(info.id);
        while (p.guiParamEvents.available() > 1)
            editor.updateEdit(info.min_value);
        editor.endEdit();
        expect(p.guiParamEvents.available() == 0, "gesture end has reserved queue capacity");
        output.limit = 0;
        paramsFlush(&p.plugin, nullptr, &output.events);
        setParam(p, info.id, info.max_value);
        output.limit = 1000000;
        paramsFlush(&p.plugin, nullptr, &output.events);
        expect(getParam(p, info.id) == info.max_value,
            "deferred notifications cannot overwrite newer host values");
        expect(output.complete(), "host backpressure preserves balanced gestures");
        editor.perform(info.id, original);
        paramsFlush(&p.plugin, nullptr, &output.events);
    }
    expect(activate(&p.plugin, 48000., 1, 64), "audio activation");
    startProcessing(&p.plugin);
    std::vector<std::vector<float>> inputs(kInputChannels, std::vector<float>(64, .001f)),
        outputs(kOutputChannels, std::vector<float>(64));
    std::vector<float*> inPtrs, outPtrs;
    for (auto& x : inputs)
        inPtrs.push_back(x.data());
    for (auto& x : outputs)
        outPtrs.push_back(x.data());
    clap_audio_buffer_t in {}, out {};
    in.channel_count = kInputChannels;
    out.channel_count = kOutputChannels;
    in.data32 = inPtrs.data();
    out.data32 = outPtrs.data();
    clap_process_t block {};
    block.frames_count = 64;
    block.audio_inputs = &in;
    block.audio_outputs = &out;
    block.audio_inputs_count = kInputChannels ? 1 : 0;
    block.audio_outputs_count = 1;
    block.out_events = &output.events;
    std::atomic<bool> stop { false }, finite { true };
    std::atomic<unsigned> blocks { 0 };
    std::thread audio([&] {
        while (!stop.load()) {
            process(&p.plugin, &block);
            for (auto& ch : outputs)
                for (auto sample : ch)
                    if (!std::isfinite(sample))
                        finite.store(false);
            ++blocks;
        }
    });
    while (!blocks.load())
        std::this_thread::yield();
    sourceImport(p);
    for (unsigned i = 0; i < 80; ++i) {
        if (hasParameters)
            editor.perform(info.id, i % 2 ? info.min_value : info.max_value);
        if (i % 20 == 0)
            render(editor, "concurrent-playback");
    }
    stop.store(true);
    audio.join();
    process(&p.plugin, &block);
    expect(finite.load(), "finite output during concurrent GUI editing");
    expect(output.complete(), "playback edit gestures balanced");
    stopProcessing(&p.plugin);
    deactivate(&p.plugin);
}
}
int main()
{
#if defined(__APPLE__)
    [NSApplication sharedApplication];
#endif
    namespace F = s3g::portable_gui::foundation;
    if (!F::acquireRuntime())
        return 2;
    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "RNBO GUI test";
    host.vendor = "s3g";
    host.version = "1";
    host.url = "";
    host.get_extension = [](const clap_host_t*, const char*) -> const void* { return nullptr; };
    host.request_restart = host.request_callback = host.request_process = [](const clap_host_t*) {};
    auto* plugin = createPlugin(&factory, &host, descriptor.id);
    expect(plugin && plugin->init(plugin), "initialization");
    if (plugin) {
        auto& p = *self(plugin);
        {
            rnbo_gui::Editor editor(p);
            run(editor, p);
        }
        nativeWindow(plugin);
        plugin->destroy(plugin);
    }
    F::releaseRuntime();
    if (passed)
        std::cout << "RNBO GUI/automation parity passed: " << S3G_RNBO_PLUGIN_NAME << '\n';
    return passed ? 0 : 1;
}
