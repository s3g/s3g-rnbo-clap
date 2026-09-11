// The Cocoa RNBO grouping, ordering and four-column geometry are retained.
// Shared s3g-dsp primitives provide typography, controls and custom menus.
namespace rnbo_gui {
namespace F = s3g::portable_gui::foundation;
namespace C = s3g::portable_gui::canvas;
using namespace VSTGUI;
constexpr uint32_t width = 980;
constexpr double contentTop = 42., panelX = 24., panelWidth = 932.;
constexpr double startX = panelX + 16., columnWidth = 214., columnStep = 226.;
constexpr double rowHeight = 42., tabWidth = 92., tabHeight = 22., tabGap = 6.;
constexpr size_t columns = 4, parametersPerPage = 24;
#if S3G_HAS_RNBO_EXPORT
struct GuiParamPage {
    std::string label;
    std::vector<size_t> indices;
};

bool splitChannelParamName(const std::string& name, std::string& channel, std::string& group)
{
    if (name.size() < 6)
        return false;
    if (name[0] != 'c' || name[1] != 'h')
        return false;
    if (!std::isdigit(static_cast<unsigned char>(name[2]))
        || !std::isdigit(static_cast<unsigned char>(name[3])) || name[4] != '_')
        return false;
    channel = name.substr(0, 4);
    group = name.substr(5);
    return !group.empty();
}

std::string groupNameForParam(const RnboParam& param)
{
    std::string channel;
    std::string group;
    if (splitChannelParamName(param.name, channel, group))
        return group;
    const auto slash = param.name.find(static_cast<char>(47));
    if (slash != std::string::npos && slash > 0)
        return param.name.substr(0, slash);
    return "params";
}

std::string displayNameForParam(const RnboParam& param)
{
    std::string channel;
    std::string group;
    if (splitChannelParamName(param.name, channel, group))
        return channel;
    const auto slash = param.name.find(static_cast<char>(47));
    if (slash != std::string::npos && slash + 1 < param.name.size())
        return param.name.substr(slash + 1);
    return param.name;
}

int channelNumberForParam(const RnboParam& param)
{
    std::string channel;
    std::string group;
    if (!splitChannelParamName(param.name, channel, group))
        return -1;
    return std::stoi(channel.substr(2));
}

std::vector<GuiParamPage> buildGuiParamGroups(const Plugin* p)
{
    std::vector<GuiParamPage> pages;
    if (!p)
        return pages;
    for (size_t i = 0; i < p->rnboParams.size(); ++i) {
        const std::string group = groupNameForParam(p->rnboParams[i]);
        auto found = std::find_if(pages.begin(), pages.end(),
            [&](const GuiParamPage& page) { return page.label == group; });
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
            if (channelA >= 0 && channelB >= 0 && channelA != channelB)
                return channelA < channelB;
            return p->rnboParams[a].name < p->rnboParams[b].name;
        });
    }
    if (pages.empty())
        pages.push_back(GuiParamPage { "params", {} });
    return pages;
}
std::vector<GuiParamPage> buildGuiParamPages(const Plugin* p)
{
    const auto groups = buildGuiParamGroups(p);
    std::vector<GuiParamPage> pages;
    for (const auto& group : groups) {
        const size_t count = std::max<size_t>(
            1, (group.indices.size() + parametersPerPage - 1) / parametersPerPage);
        for (size_t page = 0; page < count; ++page) {
            const auto first = std::min(page * parametersPerPage, group.indices.size());
            const auto last = std::min(first + parametersPerPage, group.indices.size());
            auto label = group.label;
            if (count > 1)
                label += " " + std::to_string(page + 1) + "/" + std::to_string(count);
            pages.push_back(
                { label, { group.indices.begin() + first, group.indices.begin() + last } });
        }
    }
    return pages;
}
#endif
size_t tabColumns() { return 9; }
size_t tabRows(size_t pageCount)
{
    return std::max<size_t>(1, (pageCount + tabColumns() - 1) / tabColumns());
}
double paramTop(size_t pageCount) { return 124. + (tabRows(pageCount) - 1) * 26.; }
std::string guiPluginTitle() { return F::pluginTitleText(S3G_RNBO_PLUGIN_NAME); }
std::string peakText(float peak)
{
    return C::format("PK %+4.1f", 20. * std::log10(std::max(.000001f, peak)));
}
std::string compactValue(double value)
{
    if (!std::isfinite(value))
        return "0.000";
    return C::format(
        std::abs(value) >= 100. ? "%.1f" : std::abs(value) >= 10. ? "%.2f" : "%.3f", value);
}
uint32_t nativeHeight(const Plugin* p)
{
#if S3G_HAS_RNBO_EXPORT
    const auto pages = buildGuiParamPages(p);
    size_t maxItems = 0;
    for (const auto& page : pages)
        maxItems = std::max(maxItems, page.indices.size());
    return std::max<uint32_t>(320,
        uint32_t(paramTop(pages.size()) + ((maxItems + columns - 1) / columns) * rowHeight + 104.));
#else
    (void)p;
    return 320;
#endif
}
#if S3G_HAS_RNBO_EXPORT
bool loadSource(Plugin& p, const std::string& path)
{
    if (!p.processor.hasExternalDataRef("src")) {
        p.processor.sourceStatus = "NO RNBO src";
        return false;
    }
    s3g::rnbo_lab::DecodedAudio audio;
    std::string error;
    if (!s3g::rnbo_lab::decodeAudioFile(path, audio, error)) {
        p.processor.sourceStatus = error;
        return false;
    }
    return p.processor.setExternalAudioBuffer(
        "src", std::move(audio.samples), audio.sampleCount, audio.channels, audio.sampleRate, path);
}
#endif
class Editor final : public C::View {
public:
    explicit Editor(Plugin& instance)
        : C::View(width, instance.nativeGuiHeight)
        , p(instance)
    {
#if S3G_HAS_RNBO_EXPORT
        pages = buildGuiParamPages(&p);
#endif
    }
    ~Editor() override { stopRefresh(); }
    void stopRefresh() override
    {
        C::View::stopRefresh();
        endEdit();
    }
    void request() { s3g::clap_gui::requestParamEventService(p.host, p.hostParams); }
    void dirtyState()
    {
        if (p.host && p.host->get_extension) {
            const auto* state = static_cast<const clap_host_state_t*>(
                p.host->get_extension(p.host, CLAP_EXT_STATE));
            if (state && state->mark_dirty)
                state->mark_dirty(p.host);
        }
    }
    bool beginEdit(clap_id id)
    {
        endEdit();
        if (p.guiParamEvents.available() < 2)
            return false;
        p.guiParamEvents.push({ Kind::GestureBegin, id, 0. });
        editing = id;
        request();
        return true;
    }
    void endEdit()
    {
        if (editing == CLAP_INVALID_ID)
            return;
        // updateEdit always leaves one slot reserved for this balanced end.
        p.guiParamEvents.push({ Kind::GestureEnd, editing, 0. });
        editing = CLAP_INVALID_ID;
        request();
    }
    void updateEdit(double value)
    {
        if (editing == CLAP_INVALID_ID || !std::isfinite(value) || p.guiParamEvents.available() < 2)
            return;
        p.guiParamEvents.push({ Kind::Value, editing, value });
        setParam(p, editing, value);
        request();
        dirtyState();
    }
    bool perform(clap_id id, double value)
    {
        endEdit();
        if (!std::isfinite(value))
            return false;
        const s3g::clap_gui::ParamEvent events[] = { { Kind::GestureBegin, id, 0. },
            { Kind::Value, id, value }, { Kind::GestureEnd, id, 0. } };
        if (!p.guiParamEvents.pushBatch(events, 3))
            return false;
        setParam(p, id, value);
        request();
        dirtyState();
        return true;
    }
    void randomize()
    {
        endEdit();
        if (uint64_t(paramsCount(&p.plugin)) * 3 > p.guiParamEvents.available()) {
            error = "HOST PARAMETER QUEUE FULL";
            return;
        }
        randomizeParamsWith(p, [&](clap_id id, double value) { perform(id, value); });
        error.clear();
        invalid();
    }
    void slider(clap_id id, const std::string& name, CRect frame, double minimum, double maximum,
        double def, bool compact)
    {
        const double value = getParam(p, id);
        const double norm
            = maximum > minimum ? std::clamp((value - minimum) / (maximum - minimum), 0., 1.) : 0.;
        const auto track = compact
            ? F::rect(frame.left, frame.top + 18., frame.getWidth() - 66., 8.)
            : F::rect(126., frame.top + 2., 260., 10.);
        text(C::upper(name),
            F::rect(frame.left, frame.top - 2., compact ? frame.getWidth() : 80., 15.),
            style.label);
        F::drawHorizontalSlider(
            *context, track, norm, track.top - 2., track.getHeight() + 4., style);
        text(compactValue(value),
            compact ? F::rect(frame.right - 58., track.top - 4., 58., 16.)
                    : F::rect(410., frame.top - 2., 90., 16.),
            style.value, compact ? kRightText : kLeftText);
        drag(
            frame,
            [this, id, track, minimum, maximum](CPoint pt) {
                double value = minimum
                    + std::clamp((pt.x - track.left) / track.getWidth(), 0., 1.)
                        * (maximum - minimum);
#if S3G_HAS_RNBO_EXPORT
                if (const auto* param = findRnboParam(p, id))
                    value = steppedParamValue(*param, value);
#endif
                updateEdit(value);
            },
            [this, id] { beginEdit(id); }, [this] { endEdit(); },
            [this, id, def] { perform(id, def); }, maximum > minimum);
    }
    bool dropped(const std::vector<std::string>& paths, CPoint) override
    {
#if S3G_HAS_RNBO_EXPORT
        if (paths.empty())
            return false;
        endEdit();
        const bool ok = loadSource(p, paths.front());
        if (ok)
            dirtyState();
        return ok;
#else
        (void)paths;
        return false;
#endif
    }
    void loadDialog()
    {
#if S3G_HAS_RNBO_EXPORT
        endEdit();
        F::FileDialogOptions options;
        options.title = "Load RNBO src audio";
        options.extension = "wav";
        options.extensionDescription = "WAV audio (AIFF via All files)";
        const auto path = F::runFileDialog(getFrame(), options);
        if (!path.empty() && loadSource(p, path))
            dirtyState();
#endif
    }
    void paint() override
    {
        text(guiPluginTitle(), F::rect(18., 13., 600., 16.), style.text, kLeftText, titleFont);
#if S3G_HAS_RNBO_EXPORT
        button(F::rect(640., 13., 62., 15.), "LOAD", [this] { loadDialog(); });
#endif
        button(F::rect(708., 13., 62., 15.), "RAND", [this] { randomize(); });
        const auto deviation = F::rect(778., 13., 82., 15.);
        const auto devTrack = F::rect(deviation.left + 30., 17., 24., 8.);
        text("DEV", F::rect(deviation.left, 13., 28., 15.), style.label);
        const double amount = p.randomAmount.load();
        F::drawHorizontalSlider(*context, devTrack, amount, 15., 12., style);
        text(C::format("%.2f", amount), F::rect(deviation.right - 26., 13., 26., 15.), style.value,
            kRightText);
        drag(
            deviation,
            [this, devTrack](CPoint pt) {
                p.randomAmount.store(
                    float(std::clamp((pt.x - devTrack.left) / devTrack.getWidth(), 0., 1.)));
            },
            {}, {}, [this] { p.randomAmount.store(1.f); });
#if S3G_HAS_RNBO_EXPORT
        if (p.processor.rnbo.getNumMidiInputPorts() > 0)
            F::drawButton(
                *context, F::rect(866., 13., 20., 15.), "M", font, p.midiActivity.load() > .05f);
#endif
        text(peakText(p.peak.load()), F::rect(892., 13., 70., 15.), style.value, kRightText);
#if S3G_HAS_RNBO_EXPORT
        page = std::min(page, pages.size() - 1);
        const auto& current = pages[page];
        const double top = paramTop(pages.size());
        const auto rows = (current.indices.size() + columns - 1) / columns;
        const double bottom = top + rows * rowHeight;
        panel(F::rect(panelX, contentTop, panelWidth, std::max(210., bottom + 72. - contentTop)),
            "ENGINE");
        for (size_t i = 0; i < pages.size(); ++i) {
            const auto tab = F::rect(startX + (i % tabColumns()) * (tabWidth + tabGap),
                78. + (i / tabColumns()) * 26., tabWidth, tabHeight);
            button(
                tab, C::upper(pages[i].label),
                [this, i] {
                    endEdit();
                    page = i;
                    invalid();
                },
                i == page);
        }
        for (size_t i = 0; i < current.indices.size(); ++i) {
            const auto& param = p.rnboParams[current.indices[i]];
            const auto frame = F::rect(startX + (i % columns) * columnStep,
                top + (i / columns) * rowHeight, columnWidth, 34.);
            const auto name = C::upper(displayNameForParam(param));
            if (isEnumParam(param)) {
                text(name, F::rect(frame.left, frame.top - 2., frame.getWidth(), 15.), style.label);
                std::vector<std::string> labels;
                for (const auto& label : param.enumValues)
                    labels.push_back(C::upper(label));
                const int selected = std::clamp(
                    int(std::lround(getParam(p, param.id) - param.min)), 0, int(labels.size()) - 1);
                const auto label = labels[selected];
                menu(F::rect(frame.left, frame.top + 16., frame.getWidth(), 18.), label,
                    std::move(labels), selected,
                    [this, id = param.id, minimum = param.min](
                        int index) { perform(id, minimum + index); });
            } else
                slider(param.id, name, frame, param.min, param.max, param.defaultValue, true);
        }
        const double meta = bottom + 18.;
        text("RNBO EXPORT: ON", F::rect(startX, meta, 400., 15.), style.value);
        text(C::format("IO %u IN / %u OUT", kInputChannels, kOutputChannels),
            F::rect(width - 210., meta, 186., 15.), style.value);
        text(C::upper(C::format("PARAMS %zu  PAGE %zu/%zu  GROUP %s", p.rnboParams.size(), page + 1,
                 pages.size(), current.label.c_str())),
            F::rect(startX, meta + 18., panelWidth - 32., 15.), style.value);
        text("SRC " + p.processor.sourceStatus, F::rect(startX, meta + 36., panelWidth - 32., 15.),
            style.value);
#else
        panel(F::rect(panelX, contentTop, panelWidth, 210.), "ENGINE");
        slider(kGain, "GAIN", F::rect(startX, 92., 450., 26.), 0., 1., Params {}.gain, false);
        slider(kMix, "MIX", F::rect(startX, 126., 450., 26.), 0., 1., Params {}.mix, false);
        slider(kOutput, "OUT", F::rect(startX, 160., 450., 26.), 0., 1., Params {}.output, false);
        text("RNBO EXPORT: FALLBACK DSP", F::rect(startX, 212., 350., 16.), style.value);
        text(C::format("IO %u IN / %u OUT", kInputChannels, kOutputChannels),
            F::rect(410., 212., 240., 16.), style.value);
#endif
        if (!error.empty())
            text(error, F::rect(startX, getViewSize().bottom - 24., panelWidth - 32., 16.),
                style.value);
    }
    Plugin& p;
    size_t page = 0;
    std::string error;
    clap_id editing = CLAP_INVALID_ID;
#if S3G_HAS_RNBO_EXPORT
    std::vector<GuiParamPage> pages;
#endif
private:
    using Kind = s3g::clap_gui::ParamEventKind;
};
} // namespace rnbo_gui

uint32_t preferredGuiHeight(const Plugin* p) { return rnbo_gui::nativeHeight(p); }
namespace portable = s3g::clap_gui::portable;
bool portableGuiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return portable::isApiSupported(api, floating);
}
bool portableGuiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    return portable::getPreferredApi(api, floating);
}
bool portableGuiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    auto& p = *self(plugin);
    return portable::create(p.portableGuiEditor, api, floating, [&p] {
        auto* host = new (std::nothrow) s3g::portable_gui::foundation::EditorHost(
            980, p.nativeGuiHeight, p.portableGuiWidth, p.portableGuiHeight);
        if (!host)
            return host;
        if (!host->ready() || !host->attach(new rnbo_gui::Editor(p))) {
            delete host;
            return static_cast<s3g::portable_gui::foundation::EditorHost*>(nullptr);
        }
        return host;
    });
}
void portableGuiDestroy(const clap_plugin_t* plugin)
{
    auto& p = *self(plugin);
    p.portableGuiVisible = false;
    portable::destroy(p.portableGuiEditor, [](auto* host) { delete host; });
}
bool portableGuiSetScale(const clap_plugin_t*, double) { return false; }
bool portableGuiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h)
{
    auto& p = *self(plugin);
    return portable::getSize(p.portableGuiWidth, p.portableGuiHeight, w, h);
}
bool portableGuiCanResize(const clap_plugin_t*) { return true; }
bool portableGuiGetResizeHints(const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints)
{
    return portable::getResizeHints(980, self(plugin)->nativeGuiHeight, hints);
}
bool portableGuiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h)
{
    return portable::adjustSize(980, self(plugin)->nativeGuiHeight, w, h);
}
bool portableGuiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h)
{
    auto& p = *self(plugin);
    return portable::setSize(p.portableGuiEditor, 980, p.nativeGuiHeight, p.portableGuiWidth,
        p.portableGuiHeight, w, h,
        [](auto* host, uint32_t width, uint32_t height) { return host->setSize(width, height); });
}
bool portableGuiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    return portable::setParent(self(plugin)->portableGuiEditor, window,
        [](auto* host, void* parent) { return host->setParent(parent); });
}
bool portableGuiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void portableGuiSuggestTitle(const clap_plugin_t*, const char*) { }
bool portableGuiShow(const clap_plugin_t* plugin)
{
    auto& p = *self(plugin);
    return portable::setVisible(p.portableGuiEditor, p.portableGuiVisible, true,
        [](auto* host, bool visible) { return host->setVisible(visible); });
}
bool portableGuiHide(const clap_plugin_t* plugin)
{
    auto& p = *self(plugin);
    return portable::setVisible(p.portableGuiEditor, p.portableGuiVisible, false,
        [](auto* host, bool visible) { return host->setVisible(visible); });
}
const clap_plugin_gui_t portableGui { portableGuiIsApiSupported, portableGuiGetPreferredApi,
    portableGuiCreate, portableGuiDestroy, portableGuiSetScale, portableGuiGetSize,
    portableGuiCanResize, portableGuiGetResizeHints, portableGuiAdjustSize, portableGuiSetSize,
    portableGuiSetParent, portableGuiSetTransient, portableGuiSuggestTitle, portableGuiShow,
    portableGuiHide };
