# Changelog

## 0.1.0

- Added session-wide cancellation through the optical retry ladder with a
  fixed recovery budget per read, immediate failure once the drive is no
  longer enumerable, and prompt teardown after USB detach.
- Hardened the managed-HDD cache reset: images with internal snapshots, a
  backing file, or an active lock from another process are refused, and the
  image is exclusively locked while edited.
- Stopped editing an external xemu installation's own configuration; it is
  copied into managed storage on first use and updated only there.
- Added live original Xbox disc playback from the supported
  `HL-DT-ST GP63EX70 RF02` USB drive through a read-only NBD bridge to xemu.
- Added compact playable XISO and fixed-geometry full-disc preservation with
  atomic output, readback verification, four hashes, manifests, and evidence
  sidecars.
- Added Play, Preserve, Details, Settings, and Sources pages with title-aware
  output names, file pickers, eject control, and automatic playback.
- Added managed xemu configuration, a private writable HDD, firmware import,
  2x internal rendering, widescreen scaling, and fullscreen startup.
- Corrected xemu display-enum serialization so aspect, fit, and startup-size
  changes are accepted by the pinned runtime.
- Added bounded end-to-end recovery for transient live-disc read failures and
  exact sector diagnostics when an optical error remains unrecoverable.
- Added an in-memory compact XISO view for live play so xemu receives a
  conventional filesystem layout while game data still streams from the disc.
- Added explicit read-only playback from preserved playable XISOs and
  full-disc images, with structural validation and clear active-source state.
- Made the private preservation directory the safe default and migrate legacy
  image output out of the application directory during Linux/Deck updates.
- Removed persistent host-side game-sector caching and image fallback so live
  physical-disc data remains dependent on the optical disc.
- Added a 256 KiB forward-only volatile Android read window and
  standards-based speed selection: maximum throughput on desktop and a
  balanced 2× mobile profile.
- Preserved the Xbox HDD's native X/Y/Z scratch data after clean Android
  sessions, with automatic recovery after an interrupted emulator session.
- Correctly classified user exits and physical-drive disconnects as orderly
  Android shutdowns, preventing normal unplug/reconnect cycles from erasing
  Xbox scratch data and forcing games to rebuild it from the DVD.
- Added a pointer-authentication-independent Android arm64 coroutine context,
  allowing QEMU I/O work to move between threads without an illegal-instruction
  failure and retaining asynchronous raw HDD I/O.
- Made the dedicated Android emulator process terminate with its activity and
  made the launcher wait for teardown before reclaiming the USB drive, so a
  failed launch cannot leave the interface indefinitely checking the drive.
- Prevented media-status probes from interrupting live game reads and added
  slow-read and recovery diagnostics.
- Added self-contained Linux, Steam Deck, Windows, and macOS packages with
  pinned xemu runtimes and verified third-party downloads.
- Added a Steam Deck Gaming Mode installer, controller navigation, one
  deduplicated Steam shortcut, library artwork, a scaled 1280 x 800 interface,
  and automatic focus handoff to xemu. GDOX suspends its UI input frame while
  xemu is running and waits for controller release before restoring navigation.
- Added deterministic core tests, warnings-as-errors builds, release privacy
  audits, and reproducible package tooling.
