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
`s3g-dsp/docs/gui-style-guide.md`. The current local contract tracks
`s3g-dsp` 0.6.0.

- Use flat gray/black toolbox panels on a near-black background.
- Keep titles normal weight; the dark header strip and thin top line provide
  the hierarchy.
- Use muted shared grays for labels, values, titles, borders, sliders, buttons,
  and selector menus. Avoid plugin-local bright white text.
- Keep label/control rows aligned and compact, with discrete RNBO enum params
  shown as menus rather than sliders.
- Begin the engine panel at y 42, place its title at x + 8, and place ordinary
  controls at x + 16.
- Fit panel heights to visible controls and avoid carrying dead interior space
  from one RNBO export to another.
- Keep `PK`, MIDI activity, source status, IO, page, and group information as
  compact status readouts rather than primary editable controls.
- Format `PK` in dBFS, pause timer repainting for inactive/hidden editors, and
  reset continuous controls to their declared defaults on double-click.
- Preserve the name/display split used by `s3g-dsp`: CLAP descriptors and
  macOS bundle names stay readable in REAPER, for example
  `s3g RNBO Modal Stress 24ch`, while the custom Cocoa GUI title is formatted
  at draw time as `s3g RNBO MODAL STRESS 24CH`.
- Do not fix GUI title casing by uppercasing `S3G_RNBO_PLUGIN_NAME`,
  `CFBundleName`, or the CMake-generated host name. The source of truth is
  `guiPluginTitle()` in `src/s3g_rnbo_test_clap.cpp`.
- RNBO-derived page, parameter, and enum labels are uppercased only inside the
  custom GUI. Parameter names exposed to the host/RNBO remain unchanged.

Run `./scripts/audit-gui-style.sh` after GUI or naming edits. It catches the
most likely drift: old bright colors, bold fonts, missing GUI title formatting,
panel/content geometry, hand-formatted peak values, missing double-click or
redraw guards, unsafe partial state-stream loops, and host metadata casing. If
the sibling `s3g-dsp` checkout is present, it also confirms that the upstream
style guide still carries the contracts implemented here.

Future passes can add hidden/internal parameter metadata, file dependency
loading, and multichannel layout helpers.
