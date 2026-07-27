# Android emulator core

GDOX builds its Android emulator from an exact official xemu revision and a
small, ordered patch series. The source is materialized outside this repository
by `scripts/prepare_android_emulator.sh`.

The baseline is xemu v0.8.133. It is the last SDL2 xemu release and the direct
parent of the Android work from which the platform layer was recovered.

The patch boundary is deliberate:

- `0001-android-core.patch` contains Android portability and renderer support.
- `0002-gdox-android-platform.patch` contains the native entry point, minimal
  build shell and stubs, and the boundary to GDOX-owned storage, lifecycle,
  display preferences, and physical-disc services.
- `0003-android-runtime-corrections.patch` contains audited Android fixes for
  audio conversion and pacing, emulator backend selection, mobile-safe Vulkan
  buffer limits, and display composition.
- `0004-android-performance.patch` contains bounded hot-path improvements for
  APU guest-memory reads and short-deadline timer waits.
- `0005-dsp-interpreter-fast-paths.patch` avoids redundant loop and interrupt
  bookkeeping in the MCPX audio DSP interpreter's common execution path.
- `0006-android-arm64-coroutines.patch` provides an AAPCS64 coroutine context
  that remains valid when QEMU moves work between Android I/O threads.
- `gdox_qemu_disc.c` is GDOX's physical-disc QEMU block driver and remains
  ordinary first-party source rather than part of the recovered patch.
- `sdl2/patches` contains the small pinned SDL2 Android compatibility layer
  for current broadcast-receiver security rules and reliable blocking audio
  writes.

The series stops before hakuX's later speculative CPU, GPU, cache, draw-order,
and frame-pacing changes. It also leaves worker placement to Android instead of
restricting every emulator subsystem to a fixed CPU subset. New emulator
changes must be narrow, documented by the subsystem they affect, and tested on
hardware before entering this series.

## Provenance and licensing

xemu and QEMU code retain their original copyright and GPL/LGPL licensing.
The Android portability work was recovered from the HakuX history recorded in
`android/dependencies.lock`, then expressed as an audited patch series against
the pinned official xemu base. GDOX owns its original code and maintains this
fork, but does not claim authorship of upstream work.

An Android binary is a combined GPL-2.0 derivative. A release must include the
complete corresponding source, this patch series, build scripts, and third
party notices.
