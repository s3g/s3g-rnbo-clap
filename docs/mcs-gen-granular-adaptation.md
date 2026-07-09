# mcs.gen~ Granular Adaptation Notes

Source patch:

```text
/Users/s3g/Documents/Max_s3g/HOAFX/April2026/3OAFX_spectral/3OAFX-granular-v2-example.maxpat
```

Related DSP file:

```text
/Users/s3g/Documents/Max_s3g/HOAFX/April2026/3OAFX_spectral/3OAFX-granular-v2.gendsp
```

This patch is a sibling direction to `s3g Ambi Grain Processor`. It is built as
an `mcs.gen~` grain engine, with one important musical rule: grain decisions are
shared across the 16 encoded channels, so the ambisonic field stays coherent.

## Adaptation Boundary

The current CLAP wrapper can host an RNBO C++ export and reflect RNBO parameters.
It does not yet load an external audio file into an RNBO `Buffer src`.

This Max patch cannot be dropped into the wrapper as-is because it is an
`mcs.gen~` patch, not an RNBO export. There are two practical paths:

- Create an RNBO patch that encapsulates the same engine and exposes a supported
  external buffer/data mechanism for `src`.
- Hand-port the grain engine to native C++ using the existing `s3g-dsp`
  `AmbiGrainProcessor` file-loading pattern.

For the RNBO path, the next hard requirement is a small exported RNBO test patch
that contains a named sample buffer/data object. That export will let us verify
the exact generated C++ API for loading external sample data.

## Source File Handling

The wrapper-side loader should follow the `s3g-dsp` sample-loading pattern:

- open Finder from the plugin GUI
- read with AVFoundation on macOS
- convert into interleaved float frames
- cap loaded channels to the plugin width
- publish the loaded sample through a non-realtime swap
- store only the file path in CLAP state
- reload the file path on state restore

For this granular patch the first target width is 16 channels, matching 3OA
ACN/SN3D media. Lower-channel files should be rejected or intentionally mapped;
they should not be silently treated as valid 3OA material.

## Grain Coherence Rule

Do not run one independent grain generator per channel.

The engine should use:

- one shared trigger stream
- one shared source position per grain
- one shared duration per grain
- one shared pitch/reverse/window decision per grain
- one shared output timeline/tape position
- one sample read per active encoded channel at that same grain phase

That is the part that preserves the spatial image.

## Parameters From The Max Patch

Core:

- `duration_ms`
- `duration_spread`
- `density`
- `offset`
- `offset_spread`
- `offset_scan_rate`
- `offset_scan_depth`
- `freeze`
- `freeze_window`

Pitch:

- `pitch_semitones`
- `pitch_spread_semitones`
- `pitch_quantize`
- `pitch_scale`
- `pitch_root`
- `pitch_spread_steps`
- `pitch_octave_spread`

Trigger and stability:

- `trigger_mode`
- `trigger_jitter`
- `dropout_prob`
- `adaptive_holdoff_amt`
- `adaptive_holdoff_max_ms`

Shape and output:

- `window_shape`
- `window_skew`
- `reverse_prob`
- `density_comp`
- `output_gain`
- `output_saturation`
- `tape_feedback`

## Defaults Observed In The Max Patch

The saved patch state includes several useful starting regions:

- `duration_ms` around `140`, `density` around `125`, `offset` around `0.277`
- short-grain region: `duration_ms` around `3` to `6`, `density` around `20` to
  `1000`
- long-grain region: `duration_ms` around `1000` to `2000`, `density` around
  `5` to `300`
- `window_shape` and `window_skew` centered around `0.5`
- `density_comp` often at `1.0`
- `adaptive_holdoff_amt` around `0.2`, with `adaptive_holdoff_max_ms` around
  `2` to `5`

For a realtime CLAP build, density and duration should be clamped together as in
`s3g Ambi Grain Processor` so high-density long-grain settings cannot overload
the audio thread.

## First Prototype Recommendation

The first useful prototype should be called something like:

```text
s3g RNBO 16ch Coherent Grain Test
```

It should prove only these points:

- the wrapper advertises a fixed 16-channel output bus
- Finder loading reaches a named RNBO sample buffer or the native C++ grain
  sample store
- a loaded 16-channel file plays grains with locked source positions across
  channels
- transport play/pause/stop does not corrupt the grain tape
- density and grain length are bounded by a realtime overlap cap

After that works, the full parameter set can be organized into the normal s3g
toolbox GUI style.

## Current Patch Scaffold

The ignored local work folder is:

```text
max_patches/3oafx_granular_v2_rnbo/
```

It contains a local snapshot of `3OAFX-granular-v2.gendsp` and notes for a first
RNBO patch. The current choice is to build an RNBO `.rnbopat` first:

```text
buffer~ src @external 1
gen~ @gen 3OAFX-granular-v2 @exposeparams 1
out~ 1 ... out~ 16
```

The `gen~` object should have the varname `coherent_grain`, so exposed Gen params
are grouped under names like `coherent_grain/density`. A Max `rnbo~` wrapper can
come after this exports cleanly.

The original Gen code should remain one shared 16-channel grain engine. The
first export is also the test that will tell us how RNBO exposes the named
`src` buffer to generated C++.
