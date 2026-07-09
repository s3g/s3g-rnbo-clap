# RNBO CLAP Wrapper Notes

The first wrapper is intentionally small:

- fixed input/output channel count selected at CMake configure time
- fallback DSP when no RNBO export is present
- `RNBO::CoreObject` processing when an export folder is supplied
- reflected RNBO parameters plus wrapper utility controls
- custom Cocoa GUI following the grayscale `s3g-dsp` family direction

The wrapper now reflects visible RNBO parameters, forwards host MIDI to RNBO,
groups large parameter sets into GUI pages, and provides randomization/deviation
controls for fast stress testing.

Future passes can add hidden/internal parameter metadata, file dependency
loading, and multichannel layout helpers.
