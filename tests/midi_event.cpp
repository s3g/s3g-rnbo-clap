#include "s3g_rnbo_midi.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool decode(const clap_event_header_t* event, uint32_t sampleOffset,
    std::array<uint8_t, 3u> bytes)
{
    s3g::rnbo_clap::MidiMessage message;
    return check(s3g::rnbo_clap::decodeMidiEvent(event, message),
               "event was not decoded")
        && check(message.sampleOffset == sampleOffset,
            "sample offset was not preserved")
        && check(message.data == bytes, "MIDI bytes were not preserved");
}

} // namespace

int main()
{
    bool ok = true;

    clap_event_midi_t raw {};
    raw.header = { sizeof(raw), 17u, CLAP_CORE_EVENT_SPACE_ID,
        CLAP_EVENT_MIDI, CLAP_EVENT_IS_LIVE };
    raw.port_index = 0u;
    raw.data[0] = 0x94u;
    raw.data[1] = 64u;
    raw.data[2] = 100u;
    ok &= decode(&raw.header, 17u, { 0x94u, 64u, 100u });

    raw.data[0] = 0x84u;
    raw.data[2] = 48u;
    ok &= decode(&raw.header, 17u, { 0x84u, 64u, 48u });

    raw.data[0] = 0x94u;
    raw.data[2] = 0u;
    ok &= decode(&raw.header, 17u, { 0x94u, 64u, 0u });

    raw.data[0] = 0xb4u;
    raw.data[1] = 74u;
    raw.data[2] = 91u;
    ok &= decode(&raw.header, 17u, { 0xb4u, 74u, 91u });

    raw.port_index = 1u;
    s3g::rnbo_clap::MidiMessage message;
    ok &= check(!s3g::rnbo_clap::decodeMidiEvent(&raw.header, message),
        "raw MIDI on an undeclared port was accepted");
    raw.port_index = 0u;
    raw.header.size = sizeof(clap_event_header_t);
    ok &= check(!s3g::rnbo_clap::decodeMidiEvent(&raw.header, message),
        "short raw MIDI event was accepted");

    clap_event_note_t note {};
    note.header = { sizeof(note), 23u, CLAP_CORE_EVENT_SPACE_ID,
        CLAP_EVENT_NOTE_ON, CLAP_EVENT_IS_LIVE };
    note.note_id = -1;
    note.port_index = 0;
    note.channel = 4;
    note.key = 67;
    note.velocity = 100.0 / 127.0;
    ok &= decode(&note.header, 23u, { 0x94u, 67u, 100u });

    note.header.type = CLAP_EVENT_NOTE_OFF;
    note.velocity = 48.0 / 127.0;
    ok &= decode(&note.header, 23u, { 0x84u, 67u, 0u });

    note.header.type = CLAP_EVENT_NOTE_ON;
    note.velocity = 0.0;
    ok &= decode(&note.header, 23u, { 0x84u, 67u, 0u });

    note.header.type = CLAP_EVENT_NOTE_CHOKE;
    note.velocity = 1.0;
    ok &= decode(&note.header, 23u, { 0x84u, 67u, 0u });

    note.port_index = 1;
    ok &= check(!s3g::rnbo_clap::decodeMidiEvent(&note.header, message),
        "CLAP note on an undeclared port was accepted");
    note.port_index = 0;
    note.header.size = sizeof(clap_event_header_t);
    ok &= check(!s3g::rnbo_clap::decodeMidiEvent(&note.header, message),
        "short CLAP note event was accepted");
    note.header.size = sizeof(note);
    note.velocity = std::numeric_limits<double>::quiet_NaN();
    ok &= check(!s3g::rnbo_clap::decodeMidiEvent(&note.header, message),
        "non-finite CLAP note velocity was accepted");

    clap_event_header_t unrelated { sizeof(unrelated), 0u,
        CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0u };
    ok &= check(!s3g::rnbo_clap::decodeMidiEvent(&unrelated, message),
        "unrelated CLAP event was decoded as MIDI");
    unrelated.space_id = 42u;
    unrelated.type = CLAP_EVENT_MIDI;
    ok &= check(!s3g::rnbo_clap::decodeMidiEvent(&unrelated, message),
        "non-core event was decoded as MIDI");

    if (ok) std::cout << "s3g-rnbo-clap MIDI dialect test passed\n";
    return ok ? 0 : 1;
}
