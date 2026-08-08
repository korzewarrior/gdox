# gdox / steam deck

1. in desktop mode, extract the archive and run `install.sh`.
2. complete the one administrator prompt.
3. return to gaming mode and open gdox from the non-steam library.
4. add your firmware, connect a supported optical drive, and insert a disc.

d-pad and A navigate. LB/RB change pages. gdox hands the controller to the
selected emulator while a game runs.

gaming mode keeps this controller-focused window lifecycle and does not use
desktop notification-area background mode. in desktop mode, gdox uses the
normal linux notification behavior when a StatusNotifier host is present.

xbox 360 playback uses the reviewed Xenia runtimes through Proton Experimental.
images and physical media use a local read-only bridge. this package includes
the reviewed `nbdfuse` and libnbd build and uses SteamOS's `fusermount3` and
FUSE support. physical playback is limited to the exact GP63EX70/RF02 XGD2
Wave 1, XGD2 Wave 2, and XGD3 profiles. gdox reports a missing host facility
instead of starting an incomplete session. without the included bridge or
SteamOS FUSE support, all
xbox 360 playback is unavailable, including owned images.

reinstalling updates the same shortcut and leaves firmware, EEPROM, settings,
supported save data, and preservation output alone. the clean xbox HDD remains
in the release package; gdox creates no per-user game-content HDD copy.

if the deck has no administrator password, set one with `passwd` in konsole
before installing.
