# Architecture

GDOX is a layered application with one active-media session owner.
Dependencies point inward:

```text
Dear ImGui pages
      │
application snapshot and commands
      │
runtime state machine ── compact XISO view / read-only NBD export / xemu process
      │
explicit media source ─┬─ read-only image file
                       └─ optical adapter ── platform USB/SCSI transport

preservation service ── filesystem, hashes, evidence, output I/O
```

## Core

`src/core` contains platform-independent disc, XDVDFS, compact-XISO,
disc-image, preservation, evidence, hashing, session, and NBD protocol logic.
Its public contracts are in `include/gdox`. Core code does not know about
windows, dialogs, USB enumeration, or a specific optical mechanism.

Sector sources use the `gdox_sector_source` interface. Preservation therefore
operates on the same bounded read contract whether the source is a physical
disc or a test fixture.

`cmake/GdoxSources.cmake` is the single source manifest for the portable
live-disc and HDD scratch services compiled into both desktop and Android.
Android does not maintain a copied core. `scripts/audit_architecture.py`
rejects outward dependencies from core, Android dependencies in desktop layers
or public headers, and an Android build that stops consuming these shared
source sets.

## Application

`src/app/app.c` is the presentation-facing façade. It translates the runtime
snapshot into stable page state and translates user actions into typed
commands.

The runtime is split by ownership: `runtime.c` owns the worker thread, selected
media, read-only NBD export, and xemu process; `runtime_media.c` opens validated
physical or image-backed media; `runtime_preservation.c` owns the
physical-to-file workflow; and `runtime_controls.c` owns commands, preferences,
firmware imports, and source overrides. Their shared contract is private to
`src/app`. Only the worker can transition a media session. UI code never owns
an optical handle or image file.

`optical_monitor.c` is the pure state machine between passive device
observation and active session initialization. It requires stable media before
one initialization attempt. A failed attempt is latched until an explicit
Start command; a transient USB disappearance cannot create an automatic retry
loop.

Published snapshots are copied under a mutex; the user-owned settings inside
each snapshot are carried across publishes by one copy pair in `runtime.c`.
Cancellation is atomic. Output files use a `.part` path and are renamed only
after finalization.

Closing a session aborts the optical retry ladder before any thread join, so
teardown does not wait for recovery to run out. Recovery within one source
read is bounded by a fixed time budget, and a drive that is no longer
enumerable fails reads immediately instead of entering recovery.

Play does not expose the physical mastering layout directly. The runtime
locates the game partition, applies the required in-memory media patches, and
builds a virtual compact XISO view before starting the read-only NBD export.
Directory sectors are synthesized in memory while file sectors continue to
stream from the selected source. This gives xemu a conventional single-layer
layout without installing or copying the game.

Physical playback has no persistent game-sector cache or image fallback. When
the physical source is selected, every desktop NBD request for game-file data
reaches the optical source. A disc image is used only after an explicit user
selection, is never persisted as the startup source, and cannot replace a
failed physical session automatically. Image-backed sessions use the same
bounded sector and compact-view contracts while reading from the selected
file. The Android QEMU adapter coalesces nearby requests through one forward-only
256 KiB memory window because mobile QEMU commonly requests optical data in
small pieces. That window is destroyed with the session and cannot sustain a
game after USB detach. Only it and filesystem metadata created by the
compatibility view remain in memory. Successful hardware READ(12) commands,
sectors, bytes, and the last physical LBA are counted by the active drive
adapter and propagated through source adapters to the Details page. Virtual
metadata cannot increment those counters.

The managed Xbox HDD retains dashboard data and saves, but its X, Y, and Z FATX
cache metadata is restored to an empty state before every xemu launch. The
reset edits only existing QCOW2 data clusters and fails closed on an unexpected
image layout, on internal snapshots, and on an image locked by another
process. A game can repopulate those partitions during the session, which
matches original-Xbox behavior. Explicit user-selected HDD images are never
modified by this policy.

Media-status commands are suspended while xemu owns the session so they cannot
interrupt the drive's sequential read stream. A separate presence-only guard
enumerates the USB/IOKit device without sending commands. If the physical
mechanism disappears, GDOX stops xemu and closes the NBD export immediately.
Explicit media polling resumes only after xemu exits.

## Platform

`src/platform` supplies:

- POSIX and Windows process, file, storage, and preservation I/O;
- native hashing backends;
- loopback NBD socket transport;
- libusb Bulk-Only Transport on Linux;
- native SCSI pass-through over the Windows optical class driver;
- IOKit/Disk Arbitration/SCSI transport on macOS;
- separate exact-identity GP63/GP65 MT1887 and GP08/PL-2507 optical adapters.

The MT1887 mechanism accepts a transport opener and a requested speed from its
platform adapter. Desktop discovery selects the mechanism's maximum rate;
Android supplies its authorized USB file descriptor and mobile power policy.
Neither policy is compiled into the portable disc or application layers.

Each drive adapter accepts only its validated USB identity, SCSI identity, and
revision. Its volatile state transaction validates expected values, applies
only the allowlisted changes, and restores the stock state during normal
teardown and failed initialization. The MT1887 adapter selects the GP63 or
GP65 address table only after the complete identity matches; PB00 recovery
also verifies and canonicalizes its separate auxiliary field. The GP08
adapter keeps its multi-field
activation and restoration order inside its own source module, uses SCSI DATA
OUT only for those validated volatile-memory writes, and caps READ(10) at the
PL-2507 bridge's validated 32-sector transfer size. Adding another drive means
adding another source adapter with its own identity, transport, state
transaction, error recovery, and physical tests.

Drive discovery is non-owning. Linux uses libusb enumeration plus the kernel
optical media-status interface; Windows enumerates optical class devices and
validates the class driver's storage identity; macOS uses IOKit registry
state. The active transport is claimed only after stable media is observed and
remains exclusively owned through the live session, including any
operating-system reset recovery.

SteamOS resets USB storage interfaces during system resume. If Linux reports
that a claimed supported-drive interface was lost, the transport closes the
stale handle, reopens only the same allowlisted USB identity, reclaims its
bulk interface, resets the Bulk-Only session, and retries the blocked optical
read. The NBD export and xemu process remain alive while that bounded recovery
runs. If the drive does not return, the read fails and teardown remains safe
with no disc-image fallback.

## Desktop presentation

`src/ui/presentation.cpp` owns the application shell, navigation, shared
feedback, and file-dialog lifecycle. Individual pages live in separate
translation units. The UI reads snapshots and calls the application façade;
it contains no preservation or transport algorithms.

Dear ImGui keyboard and gamepad navigation are enabled so the same interface
works in Steam Deck Gaming Mode. While xemu owns a Deck session, the
presentation loop stops polling or rendering UI input. Navigation is re-armed
only after GDOX regains focus and all controller buttons are released.

## Android application

The Android launcher and source-file flow live under
`android/app/src/main`. They use Android's USB host API only for device
discovery, permission, and ownership of the operating-system file descriptor.
The physical protocol remains in the shared C optical stack.

`android/native` adapts that descriptor to the shared sector-source contract.
The launcher owns a single-threaded passive media monitor. Session generations
discard callbacks from detached or superseded devices, and two matching
observations are required before insertion or removal becomes actionable.
Launch closes the passive transport without a USB device reset before the
emulator opens its exclusive live transport. This prevents the probe/reset/
reclaim race that otherwise occurs between two independent libusb sessions.
Android sessions request DVD 2x through the drive's optional `SET CD SPEED`
command instead of the desktop maximum. This balances live-game throughput
against spindle demand on current-limited mobile USB host ports without
changing sector semantics.

`android/emulator` registers the read-only `gdox://physical-disc` QEMU block
protocol inside the emulator process. It accepts only a prepared physical
session, has no image-file fallback, and forwards requested game-file sectors
to the optical source. The compact XDVDFS directory layout remains memory-only.
USB detach requests emulator shutdown; native I/O drains before the Java-owned
connection closes. Media removal reported by the block driver also requests an
orderly shutdown, so a replacement disc always starts in a newly identified
session rather than inheriting the previous disc's virtual layout.

An unexpected USB detach creates a small cross-process recovery marker before
the emulator exits. The launcher consumes that marker only when the user
deliberately starts another session. Until then, automatic launch remains
suppressed and the interface recommends external drive power. A file marker is
used because Android does not provide coherent cross-process
`SharedPreferences` caching.

Android preserves the emulated Xbox HDD's native X/Y/Z scratch partitions
after a clean exit. This is guest-owned temporary data, not a host disc cache:
the QEMU DVD device still has no image fallback, and removing the physical
drive ends the session. A session marker is created before the emulation thread
starts and removed only after that thread returns. If termination interrupts
the emulator, the marker survives and the next launch rebuilds the FATX scratch
metadata before allowing the Xbox to boot.

The emulator is built from a pinned official xemu revision and a maintained,
ordered Android patch series. HakuX is retained only as provenance for the
original Android portability work; it is not the build baseline. GDOX-specific
Kotlin code uses its own namespace, while upstream namespaces are retained for
upstream emulator code. The combined APK is GPL-2.0 and its release must be
accompanied by corresponding source.

The Android patch set is ordered and validated as one exact aggregate diff
against the pinned official xemu revision. The build refuses a partially
applied, locally altered, or wrong-revision emulator tree. Runtime-correction
and performance patches contain safe-area integration, title-profile handoff,
audio pacing, bounded mobile allocations, aspect-correct clearing, and the
Vulkan texture-cache recovery path.

Title identity is parsed once by the shared XDVDFS layer and carried through
the live-disc, Android media, and QEMU boundaries. The Kotlin policy resolves
the title ID to a graphics profile before emulator configuration is read.
Profiles override only the settings they own. Cache signatures include the
effective graphics profile and application build; a mismatch clears only
rebuildable emulator caches.

The Android presentation uses edge-to-edge windows but lays out content inside
the union of system-bar and display-cutout insets. The same insets define the
touch-controller coordinate space, so controls remain reachable on cutout,
gesture-navigation, and foldable displays.

## Runtime bundle

GDOX discovers xemu in this order:

1. an explicit Sources-page override;
2. the verified runtime beside the executable or inside the macOS app;
3. the managed per-user runtime location;
4. a compatible external xemu installation.

The bundled blank HDD is copied once into private user data before use. The
release folder remains replaceable, while saves, firmware, and preferences
survive upgrades.

## Preservation

Compact XISO creation enumerates and rebuilds the game filesystem. Full-disc
preservation writes fixed XGD1 geometry and keeps evidence, normalization, and
unexpected read errors as separate facts. CRC32, MD5, SHA-1, and SHA-256 are
computed during output; optional verification rereads the finished image.

Catalog matching uses geometry plus PFI/DMI fingerprints. It never selects by
title or filename and never represents catalog data as authenticated `SS.bin`.

## Repository contents

The repository contains the supported product source and build graph.
Generated build trees, download caches, runtime state, and release archives
live outside the source directory.
