# Contributing

Help with other drives is the most useful thing right now. If you have a DVD
drive that is not the supported one, say so in
[Discord](https://discord.gg/TEzuUEJk4B) or open an issue with the model,
firmware revision, and USB ID. Working out what a new drive needs is the whole
point of this project being public.

## Adding a drive

Everything drive-specific lives in one file, `src/platform/mt1887_source.c`.
A new drive needs its own source adapter with:

- an exact identity check, so it can only ever run on the drive it was written
  for
- its own transport and error recovery
- proof it works on real hardware

Do not loosen the identity check on the existing adapter to make another drive
go through it. Writing to the wrong drive's memory is how drives get bricked.

## Everything else

Keep changes inside the layers described in
[Architecture](docs/ARCHITECTURE.md). Platform code stays out of the core, and
UI code calls the application API instead of talking to drives directly.

Run this before submitting:

```sh
make check
```

Never commit console firmware, keys, game images, your own file paths, device
serials, private logs, or untested drive-write commands. The release tooling
runs a privacy audit, but check your own changes too.

Contributions are dedicated under the repository's CC0 1.0 public-domain
dedication. Submit only work you have the right to dedicate.
