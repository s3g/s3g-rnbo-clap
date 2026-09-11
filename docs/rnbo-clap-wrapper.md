# RNBO CLAP VSTGUI adaptation

The wrapper builds an RNBO C++ export into CLAP, not into a Max external.
The same export and fixed channel layout feed independent Mac and Windows
build trees. Generated exports remain ignored and keep their original licenses.

## Shared foundation

The current `s3g-dsp/plugins/common/s3g_vstgui_foundation.{h,cpp}`,
`s3g_vstgui_canvas.h`, `s3g_clap_vstgui.h` and parameter event queue are used
directly through `S3G_DSP_DIR`. No native s3g-dsp DSP targets are built.

- Fira Code (10 body / 10.5 regular title), bundled with its OFL license.
- Shared grayscale palette, outline-free controls, custom popup menus and
  centered button text; no operating-system popup styling.
- Engine panel at y 42, header inset 8, control inset 16.
- Proportional 65–200% resizing and shared macOS/Windows resource paths.
- CLAP host names and IDs unchanged; only `guiPluginTitle()` and UI labels
  apply uppercase presentation after lowercase `s3g`.
- PK remains dBFS; MIDI activity and file/IO/page/group status remain present.
- Hidden editors stop refresh; continuous controls reset on double-click.

## Adaptation, not redesign

The original Cocoa editor stays in `src/s3g_rnbo_test_clap.cpp`, selectable
with `S3G_ENABLE_PORTABLE_CLAP_GUI=OFF` on Mac. The portable editor lives in
`src/s3g_rnbo_vstgui.inc`. Its channel-prefix/slash-path grouping and ordering
come from Cocoa, as do four columns, compact sliders, tabs, enums, RAND and DEV.

Large groups are now actually split at 24 parameters, and base height includes
all wrapped tabs and control rows. Previously groups could exceed the fixed
520-pixel viewport despite the intended paging contract. Top-bar MIDI and peak
readouts no longer overlap. Generic imports no longer show a patch-specific
16-channel warning.

The startup guard, soft limiter, RNBO parameter IDs and v1 state serialization
remain intact. State still recalls a source path with a RELOAD status, not
embedded samples. Main-thread file import uses RNBO's thread-safe external-data
API and retains buffers until its release callback; the startup guard request
is handed to audio atomically. Windows decodes PCM WAV/AIFF through pinned
dr_wav, with wide-path handling. Mac retains AVFoundation decoding.

## Automation and tests

The shared bounded queue delivers balanced begin/value/end events to CLAP
process/flush. GUI changes are already applied through RNBO's MultiProducer
interface or fallback atomics, so delayed host notifications never replay a
stale value. Dragging reserves an end-event slot; RAND checks queue capacity
before applying any changes.

Run `ctest --test-dir <native-build> --output-on-failure`. Tests include state
stream chunking/failure handling, GUI style, native canvas rendering, all
reflected pages, defaults, enums, RAND, queue saturation and concurrent audio
editing. Audio-file tests cover channel order, Unicode paths and failed-load
preservation. Set `S3G_RNBO_CAPTURE_DIR` to keep canvas references.

The dual-build command packages only after both outputs succeed and their
metadata/export identities match. It never installs or overwrites a previous
distribution. Copy the Windows CLAP and adjacent Resources folder together.
Windows runtime GUI, file dialogs and device-specific DPI behavior still need
testing in Windows REAPER.

## Verification on 2026-09-11

Native macOS tests passed for the fallback wrapper, the local 24-channel
DroneSynth export and the local 16-channel Ambigrain export (five tests each).
The GUI test covers native window attachment/reopening and 65–200% bounds,
plus all exposed controls. Ambigrain also exercises Unicode-path sample import,
failed-load preservation and replacement while processing. These generated
exports remain outside version control; the tests run against whichever export
is configured in CMake.

Mac and Windows x64 DroneSynth CLAPs were built together with matching build
identities. Windows execution is not validated by these Mac tests. The retained
Cocoa backend can still be built separately for regression comparisons.

API references:

- [RNBO CoreObject and thread-safe external data](https://rnbo.cycling74.com/cpp/ref/classes/core_object)
- [Using an RNBO C++ export](https://rnbo.cycling74.com/learn/how-to-include-rnbo-in-your-c-project)
- [VSTGUI](https://github.com/steinbergmedia/vstgui)
