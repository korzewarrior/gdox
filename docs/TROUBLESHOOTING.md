# Troubleshooting

This guide covers the Original Xbox and Xbox 360 desktop paths in GDOX.

## Supported drive unavailable

Confirm the identity on Details. A supported drive must report one complete
profile:

- `HL-DT-ST DVDRAM GP63EX70 RF02` and USB `0e8d:1887`;
- `HL-DT-ST DVDRAM GP65NB60 PB00` and USB `0e8d:1887`;
- `HL-DT-ST DVDRAM GP08NU10 JE01` and Prolific PL-2507 USB `152e:2507`;
- `ASUS SDRW-08D1S-U A202` and Initio USB `13fd:1640`.

A similar retail name, firmware revision, or internal mechanism is not enough.
`GP65NB60 PB01` is not supported. The GP65, GP08, and ASUS adapters also
refuse activation if any expected stock memory value differs. Close the ASUS
tray by hand; GDOX intentionally does not send a tray-load command to it.

On Linux, rerun `sudo ./setup-device-access.sh`, disconnect the drive, and
reconnect it in the active desktop session. Do not run GDOX as root.

On Steam Deck, a short USB reset during suspend and resume is recovered inside
the current live session. If the drive remains unavailable after wake, close
the game, confirm the drive still has power, and reopen GDOX. Do not repeatedly
suspend while the tray is moving.

On Windows, keep the standard Microsoft CD-ROM and USB mass-storage drivers
assigned to the drive. GDOX uses Windows' native optical command channel; a
WinUSB or other replacement driver prevents that path from opening.

## macOS says the disc was mounted first

Leave GDOX open, eject the disc, and insert it again. The mount guard normally
claims the second insertion. If the error persists, close apps that inspect
optical media, power-cycle the drive, and reopen GDOX.

A short `02/04/01` not-ready status while the tray spins up is normal; GDOX
retries it. Persistent errors are not normal.

## GDOX does not stay in the notification area

Windows and macOS provide the required notification facility directly. Linux
requires a StatusNotifier-compatible host supplied by the desktop environment.
If no host is available, GDOX keeps or restores the main window instead of
running invisibly. Steam Deck Gaming Mode intentionally uses its full-screen
controller lifecycle rather than desktop notification-area behavior.

Use `gdox --background` to request a hidden start. Choose **Open GDOX** from the
notification menu to restore the window and **Quit** to release the drive and
exit. Only one GDOX process may run at once; a second launch exits with a message
instead of competing for the optical drive.

## xemu is not ready

Open Sources. xemu, MCPX, Xbox BIOS, and Xbox HDD must all show usable paths.
Re-select any missing firmware. If using a custom xemu, clear that selection to
return to the packaged version. GDOX always uses the verified included clean
HDD as the volatile backing image.

## xemu asks for a disc

Wait until GDOX reports **Disc ready** before starting xemu. Close xemu,
power-cycle the optical drive, reopen GDOX, and try again. A damaged disc may
need cleaning and a slower retry.

## Xenia is not ready

Do not substitute an upstream Xenia binary. GDOX accepts only an exact bundled
runtime whose capability response and digest prove the save-only storage
contract. If Xenia is unavailable, keep the extracted folder together and
reinstall the same GDOX version; a missing or changed runtime is rejected before
launch.

Linux and Steam Deck run the reviewed Windows Xenia builds through Proton
Experimental. If GDOX reports that Proton is missing, install Proton
Experimental through Steam or set `GDOX_PROTON` to an executable Proton
launcher. Linux uses a read-only bridge for both owned images and physical Xbox
360 media. Generic Linux needs host `nbdfuse` and `fusermount3`; the Steam Deck
package includes its reviewed `nbdfuse` and libnbd build and uses SteamOS's
`fusermount3`. GDOX checks the bridge before committing a media session. If
either helper is missing, both owned-image and physical Xbox 360 playback are
unavailable. Original Xbox firmware does not affect Xenia readiness.

## An Xbox 360 image does not open

The image must expose a valid Xbox 360 GDFX game partition with `default.xex`
and matching XEX execution information. Renaming another console image does
not make it compatible. Windows, Linux, and Steam Deck accept validated image
files. macOS and Android do not provide Xbox 360 playback.

## Xbox 360 physical support is limited

Physical playback is limited to the exact GP63EX70/RF02 host and media
combinations in `XBOX360.md`. Similar drive names, other firmware revisions,
media profiles, and host platforms are rejected. Original Xbox support for a
drive does not imply Xbox 360 support.

## A disc image does not open

GDOX accepts a regular file whose size is a multiple of 2,048 bytes and whose
contents expose a valid original-Xbox XDVDFS game partition. Renaming another
console image to `.iso` does not make it compatible. Recreate a playable XISO
or full-disc image from Preserve, then select it explicitly on Play or Sources.

## Display changes do not affect the current game

GDOX restarts xemu after its managed display settings change. Internal scale
changes rendering resolution. Picture shape and fit affect the output surface,
but a game without a widescreen mode may still render a 4:3 camera view.

On Android, Settings changes take effect on the next emulator start. Keep
**Game compatibility profiles** enabled for known-sensitive titles. Morrowind
is intentionally limited to 1× and its original 4:3 signal because higher
internal scales can expose renderer coordinate errors. Emulator caches are
disabled, and GDOX automatically removes the exact legacy cache locations it
previously created.

Keep **Manage heat** enabled if performance falls after several minutes. GDOX
then limits only the game window's brightness when Android reports thermal
pressure and restores the system brightness on exit. The Halo and Morrowind
profiles also avoid redundant panel refreshes without changing Xbox timing.

If artifacts remain at 1×, compare Vulkan and OpenGL ES and include the
renderer in the bug report. Some title-specific emulation defects are upstream
accuracy limitations rather than optical-read failures.

## Preservation fails

Choose a writable folder with enough free space. A full-disc image requires
about 7.3 GiB plus temporary filesystem overhead. GDOX refuses to overwrite an
existing output or `.part` file.

Unexpected unreadable sectors are recorded. Clean the disc and repeat the dump
before treating the output as preservation evidence.

## Reporting a bug

Include the platform, GDOX version, exact drive model/revision, USB ID, page,
operation, and visible error text. Remove user paths, serials, firmware paths,
and game-image contents from logs before sharing them. Xenia's managed
diagnostic log exists only for the active session and is removed during
teardown; copy and redact it before closing playback if it is needed for a bug
report.
