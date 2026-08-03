#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct StateBuffer {
    std::vector<uint8_t> bytes;
    size_t cursor = 0;
    uint64_t maxChunk = std::numeric_limits<uint64_t>::max();
    bool returnOversizedCount = false;
};

int64_t stateWrite(const clap_ostream_t* stream, const void* source, uint64_t size)
{
    auto* buffer = static_cast<StateBuffer*>(stream->ctx);
    if (buffer->returnOversizedCount) return static_cast<int64_t>(size + 1u);
    const size_t amount = static_cast<size_t>(std::min(size, buffer->maxChunk));
    const auto* bytes = static_cast<const uint8_t*>(source);
    buffer->bytes.insert(buffer->bytes.end(), bytes, bytes + amount);
    return static_cast<int64_t>(amount);
}

int64_t stateRead(const clap_istream_t* stream, void* destination, uint64_t size)
{
    auto* buffer = static_cast<StateBuffer*>(stream->ctx);
    if (buffer->returnOversizedCount) return static_cast<int64_t>(size + 1u);
    const size_t available = buffer->bytes.size() - buffer->cursor;
    const size_t amount = static_cast<size_t>(
        std::min<uint64_t>(std::min<uint64_t>(size, buffer->maxChunk), available));
    if (amount == 0u) return 0;
    std::memcpy(destination, buffer->bytes.data() + buffer->cursor, amount);
    buffer->cursor += amount;
    return static_cast<int64_t>(amount);
}

struct InputEvents {
    clap_input_events_t events {};
    clap_event_param_value_t value {};
};

uint32_t eventCount(const clap_input_events_t*) { return 1u; }

const clap_event_header_t* eventGet(const clap_input_events_t* events, uint32_t index)
{
    if (index != 0u) return nullptr;
    const auto* input = reinterpret_cast<const InputEvents*>(events);
    return &input->value.header;
}

void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) {}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_rnbo_clap_state_smoke <plugin-executable>\n";
        return 2;
    }

    void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        std::cerr << "dlopen failed: " << dlerror() << '\n';
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(module, "clap_entry"));
    if (!entry || !entry->init(argv[1])) {
        std::cerr << "missing or uninitializable clap_entry\n";
        dlclose(module);
        return 1;
    }

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) == 0u) {
        std::cerr << "missing CLAP plugin factory\n";
        entry->deinit();
        dlclose(module);
        return 1;
    }
    const clap_plugin_descriptor_t* descriptor =
        factory->get_plugin_descriptor(factory, 0u);
    clap_host_t host {
        CLAP_VERSION,
        nullptr,
        "s3g-rnbo-clap state smoke",
        "s3g",
        "https://github.com/s3g/s3g-rnbo-clap",
        "0.1.0",
        nullptr,
        hostRequestRestart,
        hostRequestProcess,
        hostRequestCallback,
    };
    const clap_plugin_t* plugin = descriptor
        ? factory->create_plugin(factory, &host, descriptor->id) : nullptr;
    if (!plugin || !plugin->init(plugin)) {
        std::cerr << "failed to create CLAP plugin\n";
        if (plugin) plugin->destroy(plugin);
        entry->deinit();
        dlclose(module);
        return 1;
    }

    auto finish = [&](int status) {
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(module);
        return status;
    };
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!state || !params || params->count(plugin) == 0u) {
        std::cerr << "missing state or parameter extension\n";
        return finish(1);
    }

    clap_param_info_t info {};
    bool found = false;
    for (uint32_t i = 0; i < params->count(plugin); ++i) {
        if (params->get_info(plugin, i, &info) && info.max_value > info.min_value) {
            found = true;
            break;
        }
    }
    double original = 0.0;
    if (!found || !params->get_value(plugin, info.id, &original)) {
        std::cerr << "missing mutable parameter\n";
        return finish(1);
    }

    StateBuffer saved;
    saved.maxChunk = 3u;
    clap_ostream_t output { &saved, stateWrite };
    if (!state->save(plugin, &output) || saved.bytes.empty()) {
        std::cerr << "partial-write state save failed\n";
        return finish(1);
    }

    const double changed = std::fabs(original - info.min_value) > 1.0e-9
        ? info.min_value : info.max_value;
    InputEvents input;
    input.events.ctx = nullptr;
    input.events.size = eventCount;
    input.events.get = eventGet;
    input.value.header.size = sizeof(input.value);
    input.value.header.time = 0u;
    input.value.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    input.value.header.type = CLAP_EVENT_PARAM_VALUE;
    input.value.param_id = info.id;
    input.value.cookie = info.cookie;
    input.value.note_id = -1;
    input.value.port_index = -1;
    input.value.channel = -1;
    input.value.key = -1;
    input.value.value = changed;
    params->flush(plugin, &input.events, nullptr);

    double mutated = 0.0;
    if (!params->get_value(plugin, info.id, &mutated)
        || std::fabs(mutated - original) < 1.0e-9) {
        std::cerr << "parameter mutation failed\n";
        return finish(1);
    }

    saved.cursor = 0u;
    saved.maxChunk = 2u;
    clap_istream_t inputStream { &saved, stateRead };
    if (!state->load(plugin, &inputStream)) {
        std::cerr << "partial-read state load failed\n";
        return finish(1);
    }
    double restored = 0.0;
    if (!params->get_value(plugin, info.id, &restored)
        || std::fabs(restored - original) > 1.0e-6) {
        std::cerr << "state round trip did not restore the parameter\n";
        return finish(1);
    }

    StateBuffer badWrite;
    badWrite.returnOversizedCount = true;
    clap_ostream_t badOutput { &badWrite, stateWrite };
    if (state->save(plugin, &badOutput)) {
        std::cerr << "state save accepted an oversized callback result\n";
        return finish(1);
    }

    StateBuffer badRead = saved;
    badRead.cursor = 0u;
    badRead.returnOversizedCount = true;
    clap_istream_t badInput { &badRead, stateRead };
    if (state->load(plugin, &badInput)) {
        std::cerr << "state load accepted an oversized callback result\n";
        return finish(1);
    }

    StateBuffer truncated = saved;
    truncated.cursor = 0u;
    truncated.maxChunk = 2u;
    truncated.bytes.pop_back();
    clap_istream_t truncatedInput { &truncated, stateRead };
    if (state->load(plugin, &truncatedInput)) {
        std::cerr << "state load accepted a truncated payload\n";
        return finish(1);
    }

    std::cout << "s3g-rnbo-clap state smoke passed for "
              << descriptor->name << '\n';
    return finish(0);
}
