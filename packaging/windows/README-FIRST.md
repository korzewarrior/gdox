# gdox / windows

1. extract the complete archive, then open `gdox.exe` inside that folder.
2. add your MCPX rom and xbox bios on sources.
3. connect a supported optical drive or choose an owned image.
4. choose the available start action for the detected system.

closing the window keeps disc monitoring active in the notification area.
run `gdox.exe --background` to start hidden, then use the gdox notification
menu to open the window or quit cleanly.

xbox 360 playback uses the reviewed Xenia runtimes with save-only content
isolation. transient Xenia state uses a GDOX-owned session directory that is
removed during teardown and recovery. supported physical drives and disc
formats are listed at https://gdox.korze.org/drives/. original xbox discs
continue to use xemu.

keep this folder together. firmware, EEPROM, settings, and supported save data
live in appdata. the clean xbox HDD stays in this folder; gdox does not create a
per-user game-content HDD copy.

keep the stock cd-rom and usb storage drivers. do not install winusb or zadig.
