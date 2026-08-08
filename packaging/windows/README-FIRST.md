# gdox / windows

1. open `gdox.exe`.
2. add your MCPX rom and xbox bios on sources.
3. connect a supported optical drive or choose an owned image.
4. choose the available start action for the detected system.

closing the window keeps disc monitoring active in the notification area.
run `gdox.exe --background` to start hidden, then use the gdox notification
menu to open the window or quit cleanly.

xbox 360 playback uses the reviewed Xenia runtimes with save-only content
isolation. transient Xenia state uses a GDOX-owned session directory that is
removed during teardown and recovery. physical playback is limited to the
exact GP63EX70/RF02 XGD2 Wave 1 and Wave 2 profiles. this is not general drive
support. original xbox discs continue to use xemu.

keep this folder together. firmware, EEPROM, settings, and supported save data
live in appdata. the clean xbox HDD stays in this folder; gdox does not create a
per-user game-content HDD copy.

keep the stock cd-rom and usb storage drivers. do not install winusb or zadig.
