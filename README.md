# gdox

gdox reads, plays, and preserves physical game discs through supported optical
drives.

the current release supports original xbox. it can send a physical disc
directly to [xemu](https://xemu.app/), make a playable xiso, or create a
full-disc preservation image.

## now

0.1.3 is an active development release. current system: original xbox.

linux, steam deck, macos, and windows builds are available. android is in
development.

## source

the original xbox path reads only what xemu asks for. it does not flash the
drive or copy the game first.
[how it works](https://gdox.korze.org/wtf/#original-xbox) explains the
complete path.

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
