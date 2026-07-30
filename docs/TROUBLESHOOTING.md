# Troubleshooting

## Supported drive unavailable

Confirm the identity on Details. A supported drive must report one complete
profile:

- `HL-DT-ST DVDRAM GP63EX70 RF02` and USB `0e8d:1887`;
- `HL-DT-ST DVDRAM GP65NB60 PB00` and USB `0e8d:1887`;
- `HL-DT-ST DVDRAM GP08NU10 JE01` and Prolific PL-2507 USB `152e:2507`.

A similar retail name, firmware revision, or internal mechanism is not enough.
`GP65NB60 PB01` is not supported. The GP65 and GP08 adapters also refuse
activation if any expected stock memory value differs.

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

## xemu is not ready

Open Sources. xemu, MCPX, Xbox BIOS, and Xbox HDD must all show usable paths.
Re-select any missing firmware. If using a custom xemu or HDD, choose **Use
included** to return to the packaged version.

## xemu asks for a disc

Wait until GDOX reports **Disc ready** before starting xemu. Close xemu,
power-cycle the optical drive, reopen GDOX, and try again. A damaged disc may
need cleaning and a slower retry.

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
internal scales can expose renderer coordinate errors. **Clear emulator
caches** removes only rebuildable shader, pipeline, and translation data; it
does not remove the HDD or saves.

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
and game-image contents from logs before sharing them.
