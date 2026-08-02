# Original Xbox user guide

This guide covers current Original Xbox support in gdox.

## First run

Extract the archive for your platform without moving individual files out of
it. The bundled xemu runtime and HDD must remain beside GDOX.

### Linux

Run `sudo ./setup-device-access.sh` once, then start `./gdox`. Alternatively,
run `./install.sh` to add GDOX to the application menu and install the device
rule.

### Steam Deck

In Desktop Mode, run `install.sh`. It installs GDOX in your home directory,
adds it to Steam, and prompts once for USB-device access. Return to Gaming
Mode and launch GDOX from the Non-Steam library.

Use the D-pad to move, A to select, and LB/RB to change pages. GDOX yields the
screen and controller to xemu during play and returns when xemu closes. Choose
**Quit** at the top right to close GDOX.

### macOS

Open `GDOX.app`. An ad-hoc test build may require right-clicking the app and
choosing **Open** once. GDOX uses native USB and Disk Arbitration APIs; no
kernel extension is installed.

### Windows

Open `gdox.exe`. GDOX uses Windows' standard optical-storage driver; do not
replace it with WinUSB or install a third-party device driver.

## Firmware

Open **Sources** and select:

- a 512-byte MCPX 1.0 boot ROM;
- a compatible 256 KiB, 512 KiB, or 1 MiB Xbox BIOS.

GDOX validates the MCPX hash and firmware sizes before importing them. It
creates a private xemu configuration and writable HDD under your user
account. The source paths and readiness are visible on Sources and Details.

## Play

Connect the supported drive and insert an original Xbox disc. When the disc
name appears, choose **Start xemu** or click the disc. **Restart xemu** and
**Close xemu** control the current session. **Eject** physically opens the
tray when the drive supports it. Operate the ASUS SDRW-08D1S-U tray by hand;
GDOX disables Eject for that profile.

Enable **Auto start on insert** on Settings if desired.

Removing the disc or closing GDOX ends the live session. Disc data is served
read-only through a local session and is not installed.

### Preserved disc images

Physical discs are always the default. To use a preservation image instead,
choose **Open a preserved disc image** on Play, or choose the image on Sources.
GDOX accepts an Xbox playable XISO or a full-disc image containing a valid
XDVDFS game partition. It identifies the game and image layout before starting
xemu, then serves the file read-only through the same private local session.

The active source is shown on Play and Details. Choose **Use physical disc** to
return to the drive. Image selection lasts only for the current GDOX process:
there is no automatic image fallback, and a failed or disconnected physical
session cannot silently switch to a file.

## Display

Settings controls:

- internal rendering scale, default 2×;
- automatic, widescreen, 4:3, or native picture shape;
- centered, aspect-preserving scale, or stretch-to-fill;
- launch window size;
- fullscreen launch.

A running xemu session restarts when a display setting changes because xemu
reads these values at startup. F11 toggles xemu fullscreen after launch.
Widescreen support still depends on the game; stretching changes presentation,
not the game's camera or field of view.

## Preserve

Choose **Preserve**, select a save folder and format, and start the operation.
Preservation always starts from a physical disc, even when a disc image was
previously selected for play.
New installations default to GDOX's persistent preservation directory, which
application updates do not replace. Sources can select a different folder.

- **Playable XISO** rebuilds the game filesystem into a smaller file intended
  for xemu.
- **Full-disc preservation** creates a much larger archival image and retains
  the available disc-structure evidence.

The default filename includes the detected game title and changes when the
disc or format changes. Once you edit the name yourself, GDOX leaves it alone.

Verification rereads the completed image and compares every computed hash.
Keep the generated manifest and sidecars with a full-disc image. Cancelled or
failed operations do not publish a finished file.

## User data

Preferences, imported firmware, managed xemu configuration, and the writable
HDD live under:

- Linux/Steam Deck: `~/.config/gdox` and `~/.local/share/gdox`;
- macOS: `~/Library/Application Support/org.gdox.gdox`;
- Windows: the per-user GDOX application-data directory.

Replacing the release folder does not replace saves or imported firmware.
