# s3g RNBO CLAP

`s3g-rnbo-clap` packages instruments and effects made with Cycling '74 RNBO
as CLAP plug-ins for REAPER. Each build combines an RNBO C++ export with a
consistent, resizable `s3g` interface for macOS and Windows.

The resulting plug-ins run without Max, RNBO, the exported C++ source, or a
system-wide font installation. This repository contains the wrapper and build
tools; individual RNBO instruments and effects are distributed as built CLAP
packages.

> **Pre-release software:** REAPER is the primary host. macOS builds are tested
> natively; Windows x64 builds should be validated in Windows REAPER before
> production use.

## What You Get

- CLAP effects and instruments with fixed mono, stereo, or multichannel layouts
- A shared `s3g` interface on macOS and Windows, resizable from 65% to 200%
- Host automation and project-state recall for exposed RNBO parameters
- Automatic grouping and paging for larger parameter sets
- `RAND` and `DEV` controls for controlled parameter variation
- MIDI input for RNBO instruments and MIDI-controlled effects
- Optional audio-file loading for patches that use an RNBO data reference named
  `src`
- Startup protection and a light output limiter for experimental patches

## Install a Built Plug-in

A source checkout does not include the generated RNBO code for individual
instruments or effects. Use the prebuilt package supplied with the plug-in you
want to install.

### macOS

1. Quit REAPER.
2. Open the downloaded package and double-click
   `Install s3g-rnbo-clap CLAPs.command` if it is included.
3. If macOS blocks the installer, allow it in **System Settings → Privacy &
   Security**, then run it again.
4. Restart REAPER and rescan CLAP plug-ins if necessary.

The installer verifies the plug-in bundles, backs up an existing verified
collection, and installs into:

```text
~/Library/Audio/Plug-Ins/CLAP/s3g-rnbo-clap/
```

You can also copy a macOS `.clap` bundle to that folder manually.

### Windows

1. Quit REAPER.
2. Copy the supplied `.clap` file into a CLAP directory scanned by REAPER.
3. Copy the supplied `Resources` folder alongside the `.clap` file. Keep the
   pair together; the interface font and third-party notices live there.
4. Restart REAPER and rescan CLAP plug-ins if necessary.

## Use in REAPER

Search for `s3g` in REAPER's FX browser after installation.

For a multichannel plug-in, set the REAPER track's channel count before adding
the effect. An 8-channel build, for example, should be placed on an 8-channel
track so its pins and downstream meters expose all channels.

Instrument builds accept MIDI from the track like other software instruments.
MIDI channel numbers are preserved when events are sent to RNBO `notein` and
`ctlin` objects.

### Common interface controls

- **RAND** chooses new target values for exposed RNBO parameters.
- **DEV** controls how far values move toward those targets. Lower values make
  subtle changes; higher values explore a wider range.
- **LOAD** selects an audio file for plug-ins that read an RNBO external data
  reference named `src`.
- **PK** displays the current output peak in dBFS.
- Double-click a continuous control to restore its declared default.

WAV and AIFF are the safest choices when moving sessions between macOS and
Windows. macOS builds may also open formats supported by AVFoundation.

Projects store the selected audio-file path rather than embedding the audio.
If the file is moved, unavailable, or the project is opened on another
computer, use `LOAD` to select it again.

## Troubleshooting

**The plug-in does not appear in REAPER**

Rescan CLAP plug-ins and search for `s3g`. Confirm that the bundle is in a CLAP
location included in REAPER's plug-in paths.

**Some audio channels are missing**

Set the REAPER track channel count to match the plug-in before inserting it,
then check the plug-in pin connector.

**The Windows interface is missing resources**

Make sure the distribution's `Resources` folder remains beside the Windows
`.clap` file.

**A loaded audio file did not return with the project**

The project recalls a path, not an embedded copy. Restore the file at that path
or select it again with `LOAD`.

## For RNBO Authors

The wrapper accepts an RNBO C++ source export and builds a separately named
effect or instrument. Keep a current [`s3g-dsp`](https://github.com/s3g/s3g-dsp)
checkout beside this repository, export your RNBO patch into a subfolder of
`rnbo_exports/`, then build with the channel layout the patch expects.

On a Mac with Xcode command-line tools, CMake, and MinGW-w64 installed:

```sh
brew install cmake mingw-w64
./scripts/build-dual.sh \
  --export rnbo_exports/my_patch \
  --inputs 8 \
  --outputs 8 \
  --kind effect
```

Use `--inputs 0 --kind instrument` for a MIDI or file-driven generator. The
dual build creates matching macOS and Windows outputs without installing them.

See the following documents for technical details:

- [Building an RNBO export](docs/building.md)
- [Wrapper architecture, GUI behavior, and tests](docs/rnbo-clap-wrapper.md)
- [RNBO export folder layout](rnbo_exports/README.md)
- [Third-party licenses](THIRD_PARTY_NOTICES.md)

This wrapper creates CLAP plug-ins. It does not create Max `.mxo` or `.mxe64`
externals.

## Related Projects

- [`s3g-mc`](https://github.com/s3g/s3g-mc) provides the main `s3g` REAPER
  multichannel tools and workflows.
- [`s3g-dsp`](https://github.com/s3g/s3g-dsp) contains native C++ CLAP plug-ins
  and the shared interface foundation used here.
- [`s3g-max`](https://github.com/s3g/s3g-max) provides native Max/MSP externals
  around selected `s3g-dsp` engines.

## Licensing

Original `s3g` wrapper code in this repository is BSD-3-Clause.

RNBO-generated source has separate terms under Cycling '74's Max-Generated Code
for Export license or GPLv3. RNBO engine support source is MIT licensed. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the complete boundary.
