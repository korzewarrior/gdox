# Xbox 360 support

GDOX validates Xbox 360 media before selecting Xenia. It reads the GDFX
volume, checks `default.xex`, extracts the XEX execution identity, and selects
only compatibility settings reviewed for that exact title, media, disc number,
and disc count. Unknown titles use the conservative default policy.

Xenia is an independent project. GDOX is not affiliated with or endorsed by
Xenia, Microsoft, or Xbox.

The 0.2 Xbox 360 gameplay target is Windows x86-64, Linux x86-64, and Steam
Deck. macOS and Android do not have a compatible Xenia integration.

## Platform matrix

| Host | Owned image | Validated physical playback | ASUS A202 XGD2 | Runtime |
| --- | --- | --- | --- | --- |
| Windows x86-64 | Supported | GP63EX70/RF02 XGD2 Wave 1 and Wave 2 | Implemented; host validation not established | Reviewed native Xenia builds; transient state uses a cleanup-owned session directory |
| Linux x86-64 | Supported | GP63EX70/RF02 XGD2 Wave 1, XGD2 Wave 2, and XGD3 | Reader, stream, rendered startup, and restoration validated through Proton; stable title execution not established | Reviewed Windows Xenia builds through Proton Experimental; transient state uses a verified memory session |
| Steam Deck | Supported | GP63EX70/RF02 XGD2 Wave 1, XGD2 Wave 2, and XGD3 | Implemented; host validation not established | Same Proton and memory-session contract with the handheld performance profile |
| macOS | Unavailable | Unavailable | Unavailable | No compatible Xenia integration |
| Android | Unavailable | Unavailable | Unavailable | No Xenia integration |

Physical Xbox 360 support is an exact-profile claim, not general optical-drive
support. GDOX rejects other drive and firmware combinations. The exact ASUS
SDRW-08D1S-U/A202 XGD2 path is implemented and selected automatically. Its
Linux/Proton reader, sustained stream, rendered startup, and complete
restoration have been validated; stable execution of the tested title was not
established. Windows and Steam Deck host validation is not established. ASUS
XGD3 is not supported. Original Xbox support for the GP65NB60/PB00 and
GP08NU10/JE01 does not imply Xbox 360 support for those drives.

Windows binds the exact validated GP63 XGD2 partition directly to Xenia through
GDOX's local read-only NBD transport. Transient state is confined to a
GDOX-owned session directory and removed on teardown and recovery. No
third-party memory-filesystem driver is required.

Linux and Steam Deck bind both owned images and physical media to the exact
validated open source through a local read-only bridge. Generic Linux requires
host `nbdfuse` and `fusermount3`; if either helper is unavailable, GDOX rejects
all Xbox 360 playback before starting Xenia. The Steam Deck package includes
its reviewed `nbdfuse` and libnbd build; SteamOS provides `fusermount3` and
FUSE.

## Runtime selection and integrity

Windows uses deterministic native builds from the exact reviewed upstream
commits with GDOX's read-only transport and save-only content patches. Linux
and Steam Deck run those same executable identities through Proton
Experimental. Archive and executable sizes and SHA-256 digests are fixed in
the runtime manifest and checked during packaging and launch.

The runtime manifest pins each upstream commit and license, integration patch
and digest, native MSVC and Vulkan SDK build input, downstream archive identity,
and executable identity. The patches and exact Windows build recipe are in
`packaging/xenia/` and are included in the combined corresponding-source
archive.

The reviewed compatibility manifest is the only title-policy input. Release
validation requires its runtime revision set to match the runtime manifest,
and the compiled policy is derived from both validated inputs. Host backend,
payload identity, and Proton requirements belong to the runtime definition,
not to title compatibility settings. Adding a title never weakens drive
identity, media validation, read bounds, or restoration.

## Managed state

The runtime layer passes explicit absolute storage, cache, content, and log
paths. On Linux and Steam Deck, storage, cache, non-save content, logs, and the
Proton prefix belong to one verified memory-backed session. Only saved-game,
profile, and Xbox-saved-game content types use the persistent GDOX content
root. Hosts without a verified memory backend fail closed. Managed launches
use only a bundled executable matching the selected reviewed runtime and
advertising this exact isolation capability.

Every launch passes explicit values for the managed options supported by its
exact runtime, including disabled compatibility switches, so an existing Xenia
configuration cannot override the reviewed policy. Persistent shader storage,
the instruction information cache, guest cache mounts, and scratch mounts are
disabled. `--discord=false` prevents publication of the game title or session
state through Xenia rich presence. Linux and Steam Deck translate managed paths
to Wine paths for the Proton-backed runtime.

Host performance policy is independent of title compatibility. The Steam Deck
package sends a 720-by-480 guest display signal through the exact option pair
supported by both included revisions. This does not lower Xenia's 1x guest
rendering cost, and reviewed title policies may disable the signal. Generic
Linux and Windows packages retain Xenia's default display signal. Package
identity, not Gamescope detection or a title entry, selects this profile.

Both host profiles explicitly retain asynchronous shader compilation,
non-UI-thread presentation, unpinned guest scheduling, adaptive backend
pipeline workers, and a 60-frames-per-second ceiling when VSync is active.
Handheld launches also keep only error-level Xenia diagnostics and disable
synchronous per-batch log flushing. Native Vulkan disables relaxed FIFO
fallback, preventing a missed refresh from selecting a tearing present mode.
These are reviewed runtime-wide controls, not title workarounds.

## Physical-disc lifecycle

Linux and Steam Deck expose a validated physical game partition through a
local read-only NBD session and temporary `nbdfuse` regular-file view. Export
byte zero is the selected XGD2 or XGD3 game-partition LBA, and the export ends
at the validated source boundary. The view must report the exact partition
size before Xenia starts. Each NBD read is submitted to the optical source once;
bounded recovery belongs to the drive adapter. Shutdown order is fixed: stop
Xenia, close the file bridge, stop the export, then restore and close the drive.

While Xenia owns a physical session, the optical source serializes media-status
commands with sector reads. A physical eject-button request stops Xenia, closes
the file bridge and export, then restores and releases the drive. The GP63
completes that request with its validated tray-eject command. A replacement or
removed disc cancels the pending request and starts a complete new
identification cycle.

Xbox 360 preservation is disabled. Xbox 360 media cannot enter the Original
Xbox preservation workflow.
