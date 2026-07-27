# Project status

## Working product path

- C17/C++20 desktop application with Play, Preserve, Details, Settings, and
  Sources pages.
- Validated live play from an original Xbox disc through the GP63EX70 RF02 on
  Linux, including title discovery, both layers, read-only NBD export, xemu
  launch, eject/close handling, and volatile drive-state restoration.
- Native macOS USB/SCSI path exercised on Apple Silicon with live disc and
  xemu; mount-race recovery is implemented. Current Apple-Silicon and Intel
  packages pass native tests, privacy and code-signature checks, and GUI
  launch/quit probes on the M1 Max. Independent Apple-Silicon builds produce
  byte-identical executables and release archives.
- Native Windows 11 build and standard optical-class-driver path validated
  without WinUSB or a replacement driver. The physical Halo check entered the
  live XGD view, identified title ID `4d530004`, recorded 10 optical reads
  totaling 20,480 bytes, and restored the volatile drive state on close. The
  exact packaged GUI also passed an interactive desktop launch and graceful
  close.
- Steam Deck Gaming Mode application path exercised with the GP63, bundled
  xemu, controller-ready interface, 1280 x 800 fullscreen presentation, and
  verified focus and exclusive UI-input handoff from GDOX to xemu. Halo and
  Morrowind have booted from the live path. A forced S3 suspend reset of the
  GP63 is recovered by reopening and reclaiming its USB interface; the same
  GDOX process, xemu process, and NBD connection resumed physical reads without
  a game restart. Sustained gameplay and repeated reconnect behavior are not
  yet release-cleared.
- Compact playable XISO and fixed-geometry full-disc preservation with
  atomic output, four hashes, optional readback, manifests, logs, and
  evidence sidecars.
- Explicit read-only playback from a validated playable XISO or full-disc
  image, with physical disc retained as the non-persistent default and no
  automatic file fallback.
- Self-contained Linux, Steam Deck, Windows, Intel macOS, and Apple-Silicon
  package definitions with pinned xemu and blank HDD.
- User-selected disc-image, xemu, HDD, firmware, and output paths.
- Default 2× xemu internal resolution, widescreen controls, fullscreen, and
  auto-start preference.
- Physical optical read counters sourced below the virtual-disc layer, plus
  automatic X/Y/Z guest-cache reset for the managed HDD before each launch.
- Reproducible Android arm64 debug build with a GDOX launcher, USB-host
  permission flow, private MCPX/BIOS/HDD imports, and an in-process read-only
  QEMU block protocol. On Android 16, a physical Halo disc boots through the
  live GP63 path to the game menu with no cached game image. The foldable
  phone's 2520 × 1080 landscape cutout and gesture-navigation safe areas are
  validated for launcher and touch controls. Title-aware profiles, targeted
  emulator-cache invalidation, aspect-correct output clearing, and Vulkan
  texture-cache exhaustion recovery are integrated. The current candidate
  restores upstream APU pacing, requires exact signed-16-bit Android audio, and
  leaves emulator worker placement to Android rather than pinning every
  subsystem to the same four CPUs. A 10,328-request, 55,038,464-byte live
  optical workload completed without a dirty-disc or transport error, followed
  by a clean exit, immediate relaunch, and an in-app restart that each opened a
  new physical-disc session without reinserting the drive. The current
  launcher replaces its former mutating identification preflight with a
  generation-checked passive media monitor, stable insert/remove observations,
  reset-free handoff to the emulator, device-side eject, and automatic retry
  after transient command-channel failures. The Android package builds
  successfully. Physical detach has been verified to end the emulator and
  prevent continued play without the drive. A 256 KiB forward-only volatile
  read window, a mobile 2× optical-speed request, orderly-disconnect handling,
  and interrupted-session recovery for the Xbox HDD scratch partitions are
  integrated but still require comparative physical timing. Halo's first-level
  load, Morrowind startup, and Fable startup are not yet release-cleared.
  Listening, sustained gameplay, repeated eject/reconnect, and broader disc
  testing remain before release validation.
- Deterministic core tests, warnings-as-errors builds, privacy audits, and
  multi-platform CI definitions.

## Supported hardware

The current physical adapter supports only:

```text
HL-DT-ST DVDRAM GP63EX70 RF02
USB 0e8d:1887
```

Other enclosure revisions can contain different mechanisms. They are
unsupported until identified and implemented as separate adapters.

## Validation still required before a public 1.0

- Multiple GP63 units and a larger clean/scratched disc set.
- Sustained game sessions crossing all protected ranges.
- Suspend, forced termination, USB reset, dirty-disc, and repeated-eject
  fault matrices on every platform.
- Sustained physical validation of the native Windows optical path.
- Intel macOS physical validation.
- Android sustained play, detach shutdown, fold/unfold transitions, and
  repeated reconnect validation on physical phone hardware.
- Broader Steam Deck controller, suspend, and reconnect fault testing.
- Hardware retest of the passive-observer/one-shot initializer after an
  unexplained Steam Deck hard reset during reconnect testing.
- Developer ID signing/notarization for macOS and production code signing for
  Windows.

The build matrix proves portability of code, not physical-drive behavior.
Public wording must preserve that distinction.

## Preservation evidence limits

A full-size image is not automatically a Redump-complete submission. GDOX
reports authenticated security evidence, catalog-derived normalization,
canonical hash matches, and unexpected unreadable sectors independently.
Stock GP63 access cannot manufacture the signed challenge data in `SS.bin`.

## Release criterion

A 1.0 user should be able to extract one package, complete at most one
platform device-access step, provide legally obtained xemu firmware, insert a
disc, and play or preserve it without a terminal. No game installation,
firmware flash, disc swap, or background watcher is part of that path.
