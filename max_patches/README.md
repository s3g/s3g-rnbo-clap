# Max Patch Workspace

This folder is the local scratch/work area for Max and RNBO patch development
that feeds this wrapper repo.

Use it for:

- `.maxpat` and `.maxproj` files
- `rnboscript` experiments
- audio/media fixtures needed while designing a patch
- notes and patch-local helper files that are not ready for the repo

Everything in this folder is ignored by git except this README.

When an RNBO patch is ready to test as a CLAP plugin, export C++ source from Max
and place the export under `rnbo_exports/`, for example:

```text
rnbo_exports/8ch_passthru/
  rnbo_source.cpp
  rnbo/
    RNBO.cpp
    RNBO.h
    common/
```

Keep generated RNBO C++ source out of normal commits unless a specific export
and license path has been deliberately chosen.
