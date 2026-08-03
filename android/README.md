# gdox / android

android is still in development.

the arm64 app runs xemu with the gdox disc reader inside QEMU. it accepts only
the supported `0e8d:1887` gp63 and plays original xbox discs. xbox 360
playback is not available on android.

- no iso picker or game library
- no network access
- no disc copy written to the phone
- pulling the disc or drive ends the game

nearby reads share one 256 KiB memory window. it disappears with the session.

## storage

the game disc is never copied to android storage. each launch starts from the
pinned clean xbox hard disk and keeps ordinary guest writes, downloaded game
content, updates, and caches transient. shader caches are disabled.

saved games are persistent. the GDOX-owned save vault stores the reviewed hard
disk projection: configuration and profile data, logical files from `UDATA`,
and only `TDATA` entries covered by a positively reviewed title rule. `TDATA`
is not imported without a matching rule.

when upgrading an older installation, gdox verifies the migrated save vault by
reopening it and projecting it onto a clean hard disk before removing any
legacy managed hard disk. deletion also requires complete source projection.
if a same-path save or configuration entry differs, gdox retains the current
vault entry, merges nonconflicting saves, continues playback, and keeps the
legacy source. unclassified `TDATA` has the same preserve-and-continue result.

## current state

on android 16, physical original Xbox playback reaches the game menu without a
cached image. drive detach, relaunch, touch controls, foldable safe areas, and
volatile hard-disk writes work. longer play and broader game testing remain.

## setup

open sources and add:

- a 512-byte MCPX rom
- a compatible 1 MiB xbox bios

the build contains the exact clean xbox dashboard drive pinned in the runtime
manifest. the app verifies that immutable base before launch. writes outside
the reviewed persistent save projection remain in memory for the emulator
session.

connect the drive through a powered usb-c host adapter, grant access, and
insert the disc.

## build

the build needs JDK 21, android platform 36, Build Tools 36.1.0,
NDK 29.0.14206865, and CMake 3.30.3.

```sh
scripts/build_android.sh debug
```

the script fetches the pinned xemu, SDL2, libusb, and clean dashboard drive and
writes the apk under `../gdox-output/release/android`.

release builds need an external signing configuration and a complete
corresponding-source archive:

```sh
make android-source
```

## license

the combined android app is GPL-2.0. every public apk must include the exact
source used to build it.
