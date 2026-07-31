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

0.1.3 is an active development release. current exact drive profiles:

1. `HL-DT-ST GP63EX70 RF02` — direct-disc play validated on linux, steam
   deck, macos, and windows by korze (`@korzewarrior`).
2. `HL-DT-ST GP65NB60 PB00` — direct-disc play and restoration validated on
   windows by The Legendary Gojira (`@skullcandy977`).
3. `HL-DT-ST GP08NU10 JE01` — activation and xbox-sector reads reported by
   Eddi (`@eddifpv`); project-side live-play testing remains.
4. `ASUS SDRW-08D1S-U A202` — direct-disc play and restoration validated on
   windows by The Legendary Gojira (`@skullcandy977`).

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
