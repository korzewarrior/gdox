# gdox

put an original xbox game disc in a compatible stock dvd drive, open gdox,
and [xemu](https://xemu.app/) plays it straight from the disc. no install or
disc image first.

gdox can also make a playable xiso or a full-disc archival image.

[download](https://gdox.korze.org/download/) /
[drives](https://gdox.korze.org/drives/) /
[wtf](https://gdox.korze.org/wtf/) /
[discord](https://discord.gg/TEzuUEJk4B)

## now

0.1.0 is an active development release. direct-disc playback currently needs
the stock `HL-DT-ST GP63EX70` with `RF02` firmware.

linux, steam deck, macos, and windows builds are available. android is still
in development.

bring your own MCPX rom, xbox bios, and discs. no microsoft software or game
data is included.

## source

during play, gdox reads only what xemu asks for. it does not flash the drive
or copy the game first. [how it works](https://gdox.korze.org/wtf/) explains
the complete path.

build and test:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev --output-on-failure
```

[contributing](CONTRIBUTING.md) /
[status](docs/STATUS.md) /
[safety](docs/SAFETY.md) /
[license](https://github.com/korzewarrior/license)
