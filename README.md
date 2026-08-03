# gdox

gdox reads, plays, and preserves physical game discs through supported optical
drives.

the 0.2 update keeps the complete original Xbox desktop workflow and
adds Xbox 360 playback on windows x86-64, linux x86-64, and steam deck. it can
send a supported physical disc directly to the selected emulator, play an owned
image, make an original Xbox playable xiso, or create an original Xbox
full-disc preservation image.

original Xbox gameplay is implemented on windows, linux, steam deck, macos,
and android; the android build remains in development. Xbox 360 gameplay
targets windows x86-64, linux x86-64, and steam deck. macos and android do not
have a compatible Xenia integration. physical original Xbox play is limited to
the four exact drive profiles in [status](docs/STATUS.md). physical Xbox 360
play is limited to the exact GP63EX70/RF02 profile and media combinations in
[Xbox 360 support](docs/XBOX360.md).

## now

GDOX is an active development release. original Xbox and Xbox 360 support are
available on the platform and drive combinations documented above.

linux, steam deck, macos, and windows builds are available. android is in
development.

## source

the original Xbox path reads only what xemu asks for. it does not flash the
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
[license](LICENSE)
