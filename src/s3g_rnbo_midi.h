#pragma once

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g::rnbo_clap {

struct MidiMessage {
    uint32_t sampleOffset = 0u;
    std::array<uint8_t, 3u> data {};
};

inline bool decodeMidiEvent(const clap_event_header_t* event,
    MidiMessage& message)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;

    if (event->type == CLAP_EVENT_MIDI) {
        if (event->size < sizeof(clap_event_midi_t)) return false;
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        if (midi->port_index != 0u) return false;
        message.sampleOffset = event->time;
        std::copy(std::begin(midi->data), std::end(midi->data),
            message.data.begin());
        return true;
    }

    if (event->type != CLAP_EVENT_NOTE_ON
        && event->type != CLAP_EVENT_NOTE_OFF
        && event->type != CLAP_EVENT_NOTE_CHOKE) {
        return false;
    }
    if (event->size < sizeof(clap_event_note_t)) return false;

    const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
    if (note->port_index != 0) return false;
    if (!std::isfinite(note->velocity)) return false;
    const int channel = std::clamp(static_cast<int>(note->channel), 0, 15);
    const int key = std::clamp(static_cast<int>(note->key), 0, 127);
    const bool on = event->type == CLAP_EVENT_NOTE_ON
        && note->velocity > 0.0;
    const double velocity = std::clamp(note->velocity, 0.0, 1.0);

    message.sampleOffset = event->time;
    message.data = {
        static_cast<uint8_t>((on ? 0x90 : 0x80) | channel),
        static_cast<uint8_t>(key),
        static_cast<uint8_t>(on
            ? std::clamp(static_cast<int>(std::lround(velocity * 127.0)),
                0, 127)
            : 0),
    };
    return true;
}

} // namespace s3g::rnbo_clap
