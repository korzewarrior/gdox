# GameCube direct-disc playback

GDOX includes Dolphin for GameCube playback on Linux x64. The supported reader
profile is the LG GP63EX70 with RF02 firmware (USB ID `0e8d:1887`). Other DVD
drives and firmware revisions are not interchangeable with this profile.
Windows and macOS GameCube playback and remote-drive playback are not supported.
For Wii discs, see [Wii playback](wii-live.md).

Insert a GameCube disc, select the supported drive in GDOX, and choose
**Start Dolphin**. Auto start on insert also works. No separate Dolphin
installation, firmware file, or game image is required.

The game window includes Dolphin's **Controllers** and **Graphics** settings.
A new controller profile uses a connected SDL gamepad when one is available;
existing mappings are preserved. Turn the controller on before starting the
first session. Use **Controllers** to select and configure a different pad.
Fullscreen playback follows the GDOX setting; other GameCube graphics settings
are configured in Dolphin.

## Storage and disc access

GDOX reads sectors on demand from the physical disc. Its temporary read-only
`disc.iso` endpoint is a virtual stream, not an image stored on the computer.
There is no persistent disc-sector cache, image creation, or full-disc preload.
Dolphin's full-game preload and frame, audio, microcode and texture dumping are
disabled for managed playback. Game saves, controller settings and shader
caches are stored separately in GDOX's private user-data directory.

The reader keeps a bounded 16 MiB decoded RAM buffer and a 768 KiB raw buffer.
Sequential access enables up to four read-ahead streams within that same
buffer. Demand reads take priority between optical commands. Every returned
sector must pass its identity and error-detection checks, including after
error correction. First reads, distant seeks, and damaged sectors can still
cause loading delays; performance depends on the disc and drive.

The RF02 reader changes only transient drive RAM and restores it after each
acquisition. It does not flash firmware. GDOX retains ownership if restoration
fails and keeps the failure visible so cleanup can be retried. Close playback
through Dolphin or GDOX before disconnecting the drive.

## Requirements and scope

The Linux package includes the disc-stream helper and the pinned Dolphin
runtime. The host still needs working graphics drivers, FUSE 3 with
`fusermount3`, and optical-device access. Run the included device-access setup
when GDOX reports that permission is missing.

Hardware validation is recorded in `STATUS.md`. A successful boot is not a
claim that every game, controller, or drive with a similar model name has been
validated. The ASUS diagnostic bridge is not advertised as GameCube playback
support.

## Building

Linux builds enable `GDOX_BUILD_GAMECUBE` by default when optical support is
built. `-DGDOX_BUILD_GAMECUBE=OFF` builds without the reader. The integrated
backend uses the normal GDOX application and shared read-only disc bridge.

Dolphin's pinned revision, runtime inventory and source identities are recorded
in `packaging/runtime-manifest.json`. Its build recipe and maintained patches
are in `packaging/dolphin`. The combined corresponding-source release includes
the pristine Dolphin tree with its submodules, GDOX patches and recipe, and
corresponding sources for all bundled libraries. Dolphin remains a separate
GPL-2.0-or-later program; its notices remain beside the runtime.
