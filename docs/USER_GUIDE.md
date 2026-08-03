# GDOX user guide

This guide covers Original Xbox support and the 0.2 Xbox 360 path for Windows
x86-64, Linux x86-64, and Steam Deck. macOS and Android do not provide Xbox 360
gameplay. The exact platform matrix is in `XBOX360.md`.

## First run

Extract the archive for your platform without moving individual files out of
it. Bundled emulator runtimes and the xemu HDD must remain beside GDOX.

### Linux

Run `sudo ./setup-device-access.sh` once, then start `./gdox`. Alternatively,
run `./install.sh` to add GDOX to the application menu and install the device
rule.

### Steam Deck

In Desktop Mode, run `install.sh`. It installs GDOX in your home directory,
adds it to Steam, and prompts once for USB-device access. Return to Gaming
Mode and launch GDOX from the Non-Steam library.

Use the D-pad to move, A to select, and LB/RB to change pages. GDOX yields the
screen and controller to the selected emulator during play and returns when it
closes. Choose **Quit** at the top right to close GDOX.

### macOS

Apple silicon requires macOS 14 or newer. Intel requires macOS 12.7.5 or
newer.

Open `GDOX.app`. If macOS refuses the first open, right-click the app and choose
**Open** once. GDOX uses native USB and Disk Arbitration APIs; no kernel
extension is installed.

### Windows

Open `gdox.exe`. GDOX uses Windows' standard optical-storage driver; do not
replace it with WinUSB or install a third-party device driver.

## Background operation

On Windows, macOS, and supported Linux desktops, closing the GDOX window keeps
disc monitoring active in the notification area. Choose **Open GDOX** from its
notification menu to restore the window, or **Quit** to stop playback, release
the drive, and exit cleanly. Starting `gdox --background` begins in this mode
without opening the main window. With **Auto start on insert** enabled, inserting
a supported disc can therefore start playback while GDOX is in the background.

Linux requires a StatusNotifier-compatible notification host. If the desktop
does not provide one, `--background` opens the normal window and closing that
window exits; GDOX never leaves an unreachable background process. Steam Deck
Gaming Mode keeps its controller-focused window lifecycle and does not use the
desktop notification-area mode.

## Saved games

Original Xbox saved games, profiles, and console settings persist between
sessions. GDOX stores the fixed HDD configuration area, logical E:\UDATA, and
only positively reviewed E:\TDATA save paths in a GDOX-owned vault and restores
them before the game starts. The hard-disk image itself remains unchanged.
Installed game data, title updates, DLC, unreviewed title data, and emulator
caches are not retained.

If an older GDOX installation has a managed per-user HDD, the eligible patched
runtime migrates only the durable configuration and save paths into the vault.
GDOX removes that old managed container only after an independent fresh proof
shows that every source save was represented, finds no unclassified title data,
and verifies the unchanged source. A differing same-path save or configuration
entry remains unchanged in the current vault; GDOX imports the nonconflicting
saves, keeps the old HDD, and continues playback. Unclassified TDATA has the
same preserve-and-continue result. User-owned HDD images and standalone xemu
data are not touched.

## Firmware

Open **Sources** and select:

- a 512-byte MCPX 1.0 boot ROM;
- a compatible 256 KiB, 512 KiB, or 1 MiB Xbox BIOS.

GDOX validates the MCPX hash and firmware sizes before importing them. It
creates a managed xemu configuration and EEPROM path under your user account.
The bundled blank HDD remains the backing for a volatile guest-write layer. A
valid existing 256-byte xemu EEPROM can be adopted once; later
changes to an external xemu configuration do not replace GDOX user data. The
source paths and readiness are visible on Sources and Details. Xbox 360
playback does not use MCPX or Xbox BIOS data.

## Play

Connect the supported drive and insert an original Xbox disc. When the disc
name appears, choose **Start xemu** or click the disc. **Restart xemu** and
**Close xemu** control the current session. **Eject** physically opens the
tray when the drive supports it. Operate the ASUS SDRW-08D1S-U tray by hand;
GDOX disables Eject for that profile.

Enable **Auto start on insert** on Settings if desired.

Removing the disc or choosing **Quit** ends the live session. Closing the main
window keeps the session and disc monitor running when background operation is
available. Disc data is served read-only through a local session and is not
installed.

During desktop live play, pressing the drive's physical eject button stops the
emulator before GDOX closes the read-only media path, restores the drive, and
releases it. The GP63, GP65, and GP08 then receive their validated tray-eject
command. The ASUS drive receives no tray command; after GDOX reports that the
drive was released, press its physical eject button again to open the tray.

### Xbox 360

Windows, Linux, and Steam Deck packages contain the reviewed Xenia Canary
revisions required by 0.2. Select an owned Xbox 360 image and wait for GDOX to
validate its GDFX volume and XEX identity before choosing **Start Xenia**. GDOX
selects the reviewed runtime and launch module for an exact title identity;
unknown titles use the conservative default policy.

Linux and Steam Deck run the same reviewed Windows executable identities
through Proton Experimental. Owned images and physical media use a local
read-only bridge so Xenia receives the exact source GDOX validated. Generic
Linux requires host `nbdfuse` and `fusermount3`; without both helpers, all Xbox
360 playback is unavailable. The Steam Deck package includes its reviewed
`nbdfuse` and libnbd build and uses SteamOS's `fusermount3`. Physical playback
is limited to the exact GP63EX70/RF02 host and media combinations in
`XBOX360.md`; it is not general drive support. macOS and Android playback are
unavailable.

Managed Xenia sessions keep saves and profiles under GDOX user data. On
Linux, other content, storage, cache, the diagnostic log, and the Proton prefix
belong to a verified memory-backed session. Shader, instruction, guest-cache,
and scratch persistence are disabled. Hosts without a verified memory-session
backend and Xenia binaries without the required save-only integration fail
closed. Discord rich presence is disabled for managed launches.

The Steam Deck package applies its handheld Xenia scheduling, shader, cache,
and logging policy on every launch. It also requests a 720-by-480 guest display
signal where the title policy permits it, but Xenia still renders the guest at
1x. Generic Linux and Windows packages retain Xenia's default display signal.
Host performance policy remains separate from per-title compatibility.

### Original Xbox preserved disc images

Physical discs are always the default. To use a preservation image instead,
choose **Open a preserved disc image** on Play, or choose the image on Sources.
GDOX accepts an Xbox playable XISO or a full-disc image containing a valid
XDVDFS game partition. It identifies the game and image layout before starting
xemu, then serves the file read-only through the same local session.

The active source is shown on Play and Details. Choose **Use physical disc** to
return to the drive. Image selection lasts only for the current GDOX process:
there is no automatic image fallback, and a failed or disconnected physical
session cannot silently switch to a file.

## Display

The following Settings controls apply to xemu playback:

- internal rendering scale, default 2× on desktop and fixed at 1× on Steam
  Deck;
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

Preferences, imported firmware, managed xemu configuration, EEPROM, and
supported save/profile data live under the paths below. Game contents, title
installs, scratch data, shader caches, and pipeline caches are not persistent
GDOX user data. Original Xbox saves use a logical UDATA pack; GDOX never makes
the whole E partition persistent merely to retain saves.

- Linux/Steam Deck: `~/.config/gdox` and `~/.local/share/gdox`;
- macOS: `~/Library/Application Support/org.gdox.gdox`;
- Windows: the per-user GDOX application-data directory.

Replacing the release folder does not replace imported firmware or supported
save/profile data.
