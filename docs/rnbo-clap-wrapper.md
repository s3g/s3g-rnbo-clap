# RNBO CLAP Wrapper Notes

The first wrapper is intentionally small:

- fixed input/output channel count selected at CMake configure time
- fallback DSP when no RNBO export is present
- `RNBO::CoreObject` processing when an export folder is supplied
- reflected RNBO parameters plus wrapper utility controls
- custom Cocoa GUI following the current grayscale `s3g-dsp` family direction

The wrapper now reflects visible RNBO parameters, forwards host MIDI to RNBO,
groups large parameter sets into GUI pages, and provides randomization/deviation
controls for fast stress testing.

## GUI Style Inheritance

RNBO wrapper GUI edits should track the corrected `s3g-dsp` CLAP style guide in
`s3g-dsp/docs/gui-style-guide.md`.

- Use flat gray/black toolbox panels on a near-black background.
- Keep titles normal weight; the dark header strip and thin top line provide
  the hierarchy.
- Use muted shared grays for labels, values, titles, borders, sliders, buttons,
  and selector menus. Avoid plugin-local bright white text.
- Keep label/control rows aligned and compact, with discrete RNBO enum params
  shown as menus rather than sliders.
- Fit panel heights to visible controls and avoid carrying dead interior space
  from one RNBO export to another.
- Keep `PK`, MIDI activity, source status, IO, page, and group information as
  compact status readouts rather than primary editable controls.

Future passes can add hidden/internal parameter metadata, file dependency
loading, and multichannel layout helpers.
