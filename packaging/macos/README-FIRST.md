# gdox / macos

apple silicon requires macos 14 or newer. intel requires macos 12.7.5 or
newer.

1. move `GDOX.app` to applications and open it.
2. add your MCPX rom and xbox bios on sources.
3. connect a supported optical drive.
4. insert an original xbox disc.

closing the window keeps disc monitoring active from the menu bar. run the
app executable with `--background` to start without its main window, then use
the gdox menu-bar item to open gdox or quit cleanly.

firmware, EEPROM, settings, and supported save data live in application
support. the clean xbox HDD stays inside the app; gdox does not create a
per-user game-content HDD copy.

no driver is installed. if macos refuses the first open, right-click the app
and choose open.

xbox 360 playback is not available on macos because no compatible Xenia
integration exists for this release.
