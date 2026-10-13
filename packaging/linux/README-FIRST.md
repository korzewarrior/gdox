# gdox / linux

1. run `sudo ./setup-device-access.sh` once.
2. connect a supported optical drive.
3. run `./gdox`.
4. for original xbox, add your MCPX rom and xbox bios on sources.
5. insert a supported disc or choose an owned image.

closing the window keeps gdox in the desktop notification area when a
StatusNotifier host is available. use `./gdox --background` to start hidden,
then use the notification menu to open gdox or quit cleanly. without a
compatible notification host, gdox keeps the window visible rather than
running as an unreachable process.

xbox 360 playback uses the reviewed Xenia runtimes through Proton Experimental.
images and physical media use the included read-only disc bridge; the host
provides `fusermount3` from FUSE 3. physical playback is limited to the exact
GP63EX70/RF02 XGD2 Wave 1, XGD2 Wave 2, and XGD3 profiles.

if either bridge helper is unavailable, all xbox 360 playback is unavailable,
including owned images. gdox reports the missing helper before starting Xenia.

the host helper is provided by `fuse3` on arch- and debian-based systems.

gamecube playback uses the included Dolphin runtime with an LG GP63EX70 RF02
drive. insert the disc and select **Start Dolphin**. use Dolphin's Controllers
and Graphics menus to configure playback. new profiles use Vulkan. disc data
is streamed through RAM; saves, settings and shader caches persist. see
`docs/gamecube-live.md` for supported hardware and limits.

keep this folder together. firmware, EEPROM, settings, and supported save data
live under your user account. the clean xbox HDD stays in this folder; gdox
does not create a per-user game-content HDD copy.

the setup script installs the optical-drive rule. run gdox as your normal
user, not root.
