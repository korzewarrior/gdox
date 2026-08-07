# Architecture

This document describes the Xbox media and playback implementation in gdox.

GDOX is a layered application with one active-media session owner.
Dependencies point inward:

```text
Dear ImGui pages
      │
application snapshot and commands
      │
runtime coordinator ── tagged playback owner ─┬─ xemu process
                                              └─ Xenia process
      │
validated media session ─┬─ read-only image file
                         └─ optical adapter ── platform USB/SCSI transport
      │
      ├─ Original Xbox ── game partition / prepared boot XBE / request-local patch / NBD
      └─ Xbox 360 ── GDFX/XEX identity / reviewed policy / stable launch view

preservation service ── filesystem, hashes, evidence, output I/O
```

## Core

`src/core` contains disc, XDVDFS, GDFX/XEX, compact-XISO, disc-image,
preservation, evidence, and hashing logic. Its public contracts are
in `include/gdox`. Operating-system services used by core are declared as
core-owned ports under `src/core/ports`; implementations live under
`src/platform`. Core code does not include platform implementation headers or
know about windows, dialogs, USB enumeration, or a specific optical mechanism.

Sector sources use the `gdox_sector_source` interface. Preservation therefore
operates on the same bounded read contract whether the source is a physical
disc or a test fixture.

`cmake/GdoxSources.cmake` is the single source manifest. Original Xbox
live-disc and HDD services compile into both desktop and Android;
compact-XISO preservation and Xbox 360 media parsing remain separate desktop
source sets. Android does not maintain a copied core. `scripts/audit_architecture.py`
rejects outward dependencies from core, including direct platform includes;
Android dependencies in desktop layers or public headers; and an Android build
that stops consuming these shared source sets. The desktop build exposes the
portable live-disc subset as `gdox::media`; optical services depend on that
target instead of the broader desktop service library. Other portable
algorithms compile into `gdox::core`. Operating-system adapters compile into
`gdox::platform`. Higher-level desktop targets compose both through the
link-only `gdox::services` interface, leaving the core target free of outward
dependencies while supplying its core-owned ports. Emulator configuration
parsing is a separate portable service used by the platform launcher.

## Application

`src/app/app.c` is the presentation-facing façade. It owns page selection,
accepts the canonical runtime snapshot, and translates user actions into typed
commands without maintaining a second field-by-field state model.

The façade and production runtime sources compile once into `gdox::runtime`.
Both the desktop executable and headless runtime-request tests link that target,
so sanitizer and warning builds cover the same implementation shipped by the
application.

The runtime is split by ownership. `runtime.c` coordinates the worker cycle;
`runtime_actions.c` applies typed user actions; `runtime_session.c` owns source
and session transitions; `runtime_media.c` validates and retains physical or
image-backed media; `runtime_playback.c` enforces one tagged emulator owner;
`runtime_xemu.c` and `runtime_xenia.c` adapt their independent backends; and
`runtime_state.c` owns canonical snapshot transitions. Preservation and
control services remain separate. Their shared contract is private to
`src/app`. Only the worker can transition a media session. UI code never owns
an optical handle, image file, export, or emulator process.

Playback ownership is assigned before launch dispatch and remains assigned
until the typed child is reaped or forcibly disposed. A failed stop cannot
publish a false stopped state or allow media teardown beneath a live child.
Architecture checks enforce the module graph and reject responsibility from
collapsing back into the worker coordinator.

`runtime_commands.c` owns the production command queue and pure action
planner. Requests retain FIFO order, own path payloads, and remain queued when
an earlier action ends a worker cycle. The worker itself is a short coordinator
over separate idle-discovery, live-session, emulator, and media-removal stages.

`optical_monitor.c` is the pure state machine between passive device
observation and active session initialization. It requires stable media before
initialization, permits two bounded retries for an unidentified transport
failure, and latches invalid media or exhausted retries until a Start command
or confirmed removal. A completed session boundary rearms discovery with the
same readiness dwell, so a replacement disc does not depend on observing an
empty tray after teardown.

Published snapshots are copied under a mutex. Preferences, runtime, and UI use
one composed settings value, which is preserved across worker publications by
a single typed assignment. Cancellation is atomic. Output files use a `.part`
path and are renamed only after finalization.

Closing a session aborts the optical retry ladder before any thread join, so
teardown does not wait for recovery to run out. Recovery within one source
read has one fixed deadline. The initial READ command, recovery commands,
state verification, speed request, retry READ, and every backoff cap their
timeouts to the remaining budget. The optical source owns that retry policy;
the NBD transport executes each request once and reports the source result. A
drive that is no longer enumerable fails reads immediately instead of entering
recovery.

During an active desktop physical session, media observation remains inside the
owned source. Its source mutex serializes nonblocking GESN and bounded TUR
commands with sector reads. Each source drains queued media events at the
session boundary. A physical eject request is bound to its media generation;
removal or replacement advances that generation and invalidates the request.
Playback must stop and the source must be restored and released before an eject
request can be completed. Failed teardown is retried without replacing the
original request identity.

Original Xbox play does not expose the physical mastering layout directly. The
runtime validates XDVDFS, locates the game partition, and exports that partition
without rebuilding its directory tree or relocating file extents. It prepares
only the bounded `/default.xbe` boot extent in a session-local memory cache
before xemu starts, applying its compatibility transform once. For reads inside
another validated XBE extent, the compatibility adapter examines only the
returned sectors and at most seven preceding bytes. It validates each XBE
header once and changes a media-check byte only when the complete signature
ends inside the current request. Request size and alignment therefore cannot
change the compatibility view or trigger a whole-file scan of secondary
executables. Compact-XISO construction remains a separate preservation service.

Physical playback has no persistent game-sector cache or image fallback. The
prepared default-XBE cache is bounded to 64 MiB, owned by one media session,
and destroyed during normal source teardown. It lets the guest's first boot
reads complete without waiting on cold optical I/O. The MT1887 source also
uses larger contiguous READ(12) commands where the platform transport permits
them. A separate live-disc adapter keeps one transport-sized read-ahead window
only after it observes a contiguous request inside a validated file extent.
The fill is capped at that file's final sector; directory sectors, mastering
gaps, random reads, and the first read of a sequence remain exact. Media change,
cancellation, or session teardown invalidates the window. A disc image is used
only after an explicit user selection, is never persisted as the startup
source, and cannot replace a failed physical session automatically.
Image-backed sessions use the same bounded game-partition contract while
reading from the selected file. The Android live-disc path uses the same
file-bounded 256 KiB window. No memory window can sustain a game after USB
detach. The prepared XBE, file read-ahead window, copied extent index, and XBE
header states all remain memory-only.
Successful hardware READ(12) commands,
sectors, bytes, and the last physical LBA are counted by the active drive
adapter and propagated through source adapters to the Details page. Virtual
metadata cannot increment those counters.

The Windows GP63 transport batches contiguous live reads up to 1 MiB so the
drive pays its command latency once per file-bounded cache fill. After one
exact read, contiguous small requests may fill a 1 MiB window on Windows GP63,
a 64 KiB window on Windows GP65, GP08, and ASUS, or a 256 KiB window on MT1887
libusb platforms. The default-XBE cache fills in 1 MiB source calls; each call
receives an independent bounded recovery deadline even when the platform
adapter splits it into smaller hardware transfers.

Xbox 360 sessions validate one of the bounded Xenia-recognized GDFX layouts,
the launch XEX, its complete execution identity, and any reviewed alternate
launch module. The compatibility policy is compiled from a normalized
manifest keyed by title, media, disc number, and disc count. Windows and Linux
retain the validated image handle while Xenia opens the same path. Runtime
descriptors own platform payload identity, graphics backend, and Proton use;
title policies contain only guest compatibility settings. A separate host
profile owns Xenia-wide performance policy. Handheld sessions send a
720-by-480 guest display signal where the selected title permits it. This is
not treated as a sub-native render scale: Xenia still renders the guest at 1x.
The launch planner composes the host profile with the title policy only after
the exact runtime advertises the managed display and performance controls.
Cross-backend scheduling, asynchronous shader work, adaptive pipeline workers,
frame limiting, and low-overhead handheld logging are pinned independently of
title compatibility. Vulkan additionally
rejects relaxed FIFO fallback so a missed refresh cannot select a tearing
present mode. For physical media,
Linux retains the validated whole-source size and selected XGD profile offset
as session metadata, then moves ownership through a partition source whose byte
zero is the selected game partition. Only that partition and its exact
remaining length enter the local NBD export and temporary read-only `nbdfuse`
view. The path cannot be replaced between validation and launch, and no full
image copy or persistent sector cache is created.

The GDOX xemu integration places the guest HDD behind a memory-backed copy-on-
write layer. Guest writes never reach the backing image. A path-aware FATX
projector is the only durable exit from that layer. Before the guest starts it
imports the fixed HDD configuration area, the logical E:\UDATA tree, and only
positively reviewed E:\TDATA save paths. It atomically checkpoints those same
paths after a guest disk flush and during orderly emulator shutdown. The vault
contains logical files, directories, FATX attributes, timestamps, and the
fixed configuration bytes needed for user profiles and console settings. It
never contains raw partitions, allocation tables, directory slack, installed
games, title updates, DLC, unreviewed title data, or X, Y, and Z scratch data.
A failed or interrupted replacement leaves the previous complete vault intact.
GDOX rejects an xemu runtime unless its exact capability response attests both
complete-HDD isolation and this schema-3 save-vault contract. Public packaging
also binds each target to a separately reviewed artifact digest outside the
runtime and integration manifests, so capability metadata cannot authorize
another binary.

The desktop migration path recognizes only the historical GDOX-owned
`xemu/xbox_hdd.qcow2` path. GDOX records the ordinary file's exact identity and
asks the reviewed xemu helper to read it once without booting, project the
durable configuration and save paths, and round-trip the resulting vault. A
receipt permits later launches to reuse that proof without rescanning the
multi-gigabyte source. The old container is removed only after a fresh proof
shows complete source projection, finds no unclassified TDATA, and an
exact-delete check proves that the same unchanged file is still held. A
differing same-path save or configuration entry remains authoritative in the
current vault; nonconflicting source saves are merged, playback continues, and
the old container is preserved. Unclassified TDATA has the same nonblocking
preservation result. A rejected migration also keeps the same-size old
container and continues playback when the existing save-vault generation
passes independent validation; an empty vault starts clean. Malformed proof
and failed validation fail closed. An HDD outside the historical managed path
is never inspected or modified. POSIX removal holds an exclusive source lock,
requires a private stable parent, revalidates the complete file identity after
hashing,
and uses a durable quarantine rename. After an interrupted removal, exactly one
private, size-bounded quarantine inode can be restored to the fixed canonical
path; fresh migration proof is still required before deletion.

Every physical source serializes media observation and sector reads on the same
transport mutex. During a live session the runtime polls that owned source at a
bounded cadence, preserving `UNKNOWN`, `ABSENT`, and `PRESENT` readiness instead
of collapsing transport errors into removal. No-medium and medium-change sense
data advance a latched generation, including when a game read observes the
change before the monitor. A generation change stops the typed playback owner,
closes the NBD export, releases the drive, and requires a complete new profile
and platform identification before auto-start. A separate presence-only guard
detects a disconnected mechanism without issuing an independent SCSI command.
Playback process polling runs before either drive probe.

## Platform

`src/platform` supplies:

- POSIX and Windows process, file, storage, and preservation I/O;
- native hashing backends;
- loopback NBD socket transport;
- separate Xenia runtime discovery and process-lifecycle adapters on Linux and
  Windows, an isolated Linux read-only file bridge, and an explicit unsupported
  adapter for hosts without a compatible Xenia runtime;
- libusb Bulk-Only Transport on Linux;
- native SCSI pass-through over the Windows optical class driver;
- IOKit/Disk Arbitration/SCSI transport on macOS;
- separate exact-identity GP63/GP65 MT1887, GP08/PL-2507, and ASUS A202/NR09
  optical adapters.

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
PL-2507 bridge's validated 32-sector transfer size. The ASUS adapter likewise
keeps its eight-field transaction and restoration order inside its source
module, validates two fixed fields before writing, caps READ(10) at 32 sectors,
and never issues a tray command to the manual-close mechanism. Adding another
drive means adding another source adapter with its own identity, transport,
state transaction, error recovery, and physical tests.

The public optical API exposes drive-independent operations only. A private
descriptor registry maps each supported identity to its name, open operation,
optional software-eject operation, and physical-request completion policy. The
GP63, GP65, and GP08 complete a physical request with their validated eject
command. The ASUS policy reports only that the source was released for manual
eject and never exposes software eject. Standard MMC command construction and
response validation live in `mmc_commands.c`; identity checks, vendor memory
commands, recovery ladders, and restoration order remain in the individual
drive adapters.

Drive discovery is non-owning. Linux uses libusb enumeration plus the kernel
optical media-status interface; Windows enumerates optical class devices and
validates the class driver's storage identity; macOS uses IOKit registry
state. Each observation batches all supported identities into one platform
enumeration pass and reports media state with the matching device. The active
transport is claimed only after stable media is observed and remains
exclusively owned through the live session, including any operating-system
reset recovery.

SteamOS resets USB storage interfaces during system resume. If Linux reports
that a claimed supported-drive interface was lost, the transport closes the
stale handle, reopens only the same allowlisted USB identity, reclaims its
bulk interface, resets the Bulk-Only session, and retries the blocked optical
read. The NBD export and active emulator process remain alive while that bounded recovery
runs. If the drive does not return, the read fails and teardown remains safe
with no disc-image fallback.

## Desktop presentation

`src/ui/presentation.cpp` owns the application shell, navigation, shared
feedback, and file-dialog lifecycle. Individual pages live in separate
translation units. The UI reads snapshots and calls the application façade;
it contains no preservation or transport algorithms.

Dear ImGui keyboard and gamepad navigation are enabled so the same interface
works in Steam Deck Gaming Mode. While an emulator owns a Deck session, the
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
session, has no image-file fallback, and forwards game-partition sectors to the
optical source through the shared live-disc pipeline.
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

Android uses the same volatile guest-HDD boundary as desktop xemu. Guest title
data and scratch remain process memory and disappear when emulation ends. The
QEMU DVD device has no image fallback, and removing the physical drive ends the
session. A small session marker still records an interrupted USB/emulator
lifecycle; it contains no disc sectors or guest filesystem data.

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
Profiles override only the settings they own. Emulator shader caches are
disabled; activation also removes narrowly identified cache directories left
by an older GDOX build.

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

The bundled blank HDD is the immutable backing for GDOX's volatile xemu layer.
No per-user HDD copy is created. The release folder remains replaceable, while
firmware, EEPROM, configuration, and the separately projected logical UDATA
save pack survive upgrades.

Closing or replacing an Original Xbox session gives xemu 15 seconds to finish
its orderly save checkpoint. A nonzero exit is reported as a checkpoint
failure. If process stop itself fails, the runtime retains ownership of the
process instead of pretending that the session has closed.

The managed xemu configuration is rebuilt from GDOX-owned paths instead of
copying an external xemu configuration. External configuration is an adoption
source only: firmware remains subject to its existing validation, and an exact
256-byte EEPROM may be copied once when managed EEPROM data does not yet exist.
The managed EEPROM, firmware, configuration, and save data are never replaced
by later external configuration changes. GDOX neither deletes standalone xemu
caches nor adopts an external HDD as runtime backing.

Xenia selection is independent of xemu discovery. A reviewed title policy
chooses one exact bundled Xenia revision. Archive and executable sizes and
SHA-256 digests are fixed in the runtime manifest and rechecked during package
creation, runtime resolution, and Linux launcher preflight. Linux assets retain
their exact upstream release provenance. Windows assets separately record the
upstream commit, GDOX integration patches, native build recipe and toolchain,
downstream archive, and executable identity. A candidate-only asset cannot enter
a release package. Each Linux launch receives storage, cache, log, non-save
content, and Proton-prefix paths below a verified memory-backed session root.
Windows uses an owned temporary-session root and removes it on normal exit or
the next recovery pass. Shader, instruction, guest-cache, and scratch
persistence are disabled. Only saved-game, profile, and Xbox-saved-game content
types use the persistent content root. The exact runtime must advertise this
GDOX storage capability. Linux physical-media preflight also requires
executable `nbdfuse` and `fusermount3` helpers before an Xbox 360 session is
committed.

Recovery removes the exact legacy GDOX-owned `xenia/storage`, `xenia/proton`,
and `xenia/logs` roots, including revisions no longer present in the catalog.
It does not broaden that deletion into `xenia/content`, which is filtered
separately to the saved-game and profile content types.

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
