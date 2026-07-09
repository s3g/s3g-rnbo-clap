# RNBO CLAP Wrapper Notes

The first wrapper is intentionally small:

- fixed input/output channel count selected at CMake configure time
- fallback DSP when no RNBO export is present
- `RNBO::CoreObject` processing when an export folder is supplied
- three wrapper parameters: `Gain`, `Mix`, `Output`
- custom Cocoa GUI following the grayscale `s3g-dsp` family direction

Future passes can add RNBO parameter reflection, metadata-based grouping,
hidden/internal parameters, file dependency loading, MIDI, and multichannel
layout helpers.
