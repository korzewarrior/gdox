# gdox

gdox plays original Xbox and Xbox 360 games from supported physical discs or
owned images. it can also make playable xisos and full-disc preservation
images from original Xbox discs.

[download](https://gdox.korze.org/download/) /
[user guide](docs/USER_GUIDE.md) /
[supported drives](https://gdox.korze.org/drives/)

## support

| platform | original Xbox | Xbox 360 |
|---|---|---|
| windows 11 x86-64 | discs, images, preservation | discs and images |
| linux x86-64 | discs, images, preservation | discs and images |
| steam deck | discs, images, preservation | discs and images |
| macos apple silicon | discs, images, preservation | — |
| macos intel | images | — |
| android arm64 | in development | — |

original Xbox physical play supports four exact optical-drive profiles. Xbox
360 validated physical play uses the exact GP63EX70/RF02 profile. The complete
host, media, and validation matrix is in [Xbox 360 support](docs/XBOX360.md).
similar drive names, firmware revisions, and USB bridges are not
interchangeable.

gdox reads game data from the disc or image while the emulator runs. saves and
profiles persist; game contents, shader caches, and session files do not.

[contributing](CONTRIBUTING.md) /
[status](docs/STATUS.md) /
[safety](docs/SAFETY.md) /
[license](LICENSE)
