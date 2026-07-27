# contributing

other drives are the main need.

send the full model, firmware revision, usb enclosure or connection, and a
clear label photo through [discord](https://discord.gg/TEzuUEJk4B) or an issue.
contact me before shipping or ordering anything.

## drive code

a new drive gets its own adapter, exact identity check, recovery path, and
physical test results. never loosen the gp63 check or try unknown drive-memory
commands on similar hardware.

## everything else

keep platform code out of the core and drive access out of the ui. the
[architecture](docs/ARCHITECTURE.md) shows the boundaries.

run:

```sh
make check
```

do not commit firmware, keys, game images, private logs, serials, or local
paths. contributions use the repository's CC0 dedication.
