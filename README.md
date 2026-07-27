# GDOX

GDOX plays original Xbox discs in xemu, straight from the disc. Plug in a
supported USB DVD drive, put a disc in, and start xemu from the GDOX window.
No ripping first, no install.

It can also make a copy: a smaller playable XISO, or a full-disc archival
image.

## Status

This is early software, not a finished product. It is free, there is nothing
to buy, and nothing here is a commercial release.

Right now it works with exactly one drive: the `HL-DT-ST DVDRAM GP63EX70`
running firmware `RF02` (USB `0e8d:1887`). That is not the goal, it is the
starting point. The drive-specific part is small and isolated, so if you have
another drive and want to help work out what it needs, that is the most useful
thing you can do. Come talk in [Discord](https://discord.gg/TEzuUEJk4B) or
open an issue.

The Steam Deck build is released. Other platforms build and run but are still
being tested.

## How it works

GDOX changes a few bytes of the drive's working memory so it will read the
Xbox game partition, then serves those sectors to xemu read-only. It does not
flash the drive, install the game, or keep a copy on your disk. The change is
undone when you close the session, and a power cycle clears it either way.

The write-up at [gdox.korze.org/wtf](https://gdox.korze.org/wtf/) explains the
details. The code is in `src/platform/mt1887_source.c`.

## What you need

- the supported drive above
- your own MCPX 1.0 ROM and Xbox BIOS

Microsoft firmware is not redistributable, so you supply your own on the
Sources page. GDOX keeps imported firmware, xemu settings, and saves in your
own user folder, so replacing the app does not touch them.

Release archives include GDOX, a pinned xemu build, a blank Xbox HDD image,
the launcher, and the docs.

## Pages

- **Play** shows the current disc and controls xemu.
- **Preserve** writes a playable XISO or a full-disc image.
- **Details** reports the drive, disc, read counters, and firmware state.
- **Settings** covers resolution, aspect, fullscreen, and auto-start.
- **Sources** picks the disc image, xemu, firmware, HDD, and output folder.

Before each launch with the managed HDD, GDOX clears the Xbox X, Y, and Z
cache partitions and leaves dashboard data and saves alone. A game can fill
them again while it runs, same as on real hardware. An HDD image you choose
yourself is never modified.

## Build

C17 for the core, C++20 for the Dear ImGui interface, raylib for the window,
CMake and Ninja to build.

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev --output-on-failure
```

On Linux you need development packages for OpenSSL, libusb, D-Bus, OpenGL,
X11, CMake, and Ninja. UI dependencies are fetched during configuration if
your system does not have compatible ones.

Run the full local check with `make check`.

See [Development](docs/DEVELOPMENT.md) and
[Architecture](docs/ARCHITECTURE.md).

## Platforms

| Platform | Application | Physical GP63 path |
|---|---:|---:|
| Linux x86-64 | Tested | Validated on Arch Linux |
| Steam Deck | Tested in Gaming Mode | Validated with the native USB path |
| macOS Apple Silicon | Tested | Validated; more fault testing to do |
| macOS Intel | Builds | Not yet tested on hardware |
| Windows 11 x86-64 | Tested | Validated with the native optical path |
| Android arm64 | Tested on Android 16 | Halo reaches the menu; more testing to do |

Exact detail is in [Project status](docs/STATUS.md). Android instructions are
in [GDOX for Android](android/README.md).

On Steam Deck in Gaming Mode, use the D-pad and A to navigate and LB/RB to
change pages. While xemu runs, GDOX hides itself and hands over the
controller, and takes it back only after xemu closes.

## Safety and licensing

Read [Safety](docs/SAFETY.md) before changing optical drivers or preserving
discs. Use GDOX only with discs and firmware you are entitled to use.

GDOX is dedicated under Korze's
[CC0 1.0 public-domain license](https://github.com/korzewarrior/license).
Bundled third-party software keeps its own license; see
[Third-party notices](THIRD_PARTY_NOTICES.md).
