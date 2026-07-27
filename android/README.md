# gdox / android

android is still in development.

the arm64 app runs xemu with the gdox disc reader inside QEMU. it accepts only
the supported `0e8d:1887` gp63.

- no iso picker or game library
- no network access
- no disc copy written to the phone
- pulling the disc or drive ends the game

nearby reads share one 256 KiB memory window. it disappears with the session.

## current state

on android 16, a physical Halo disc reaches the menu without a cached game
image. drive detach, relaunch, touch controls, foldable safe areas, and
orderly hdd recovery work. longer play and broader game testing remain.

## setup

open sources and add:

- a 512-byte MCPX rom
- a compatible 1 MiB xbox bios
- a writable raw or QCOW2 xbox hdd

connect the drive through a powered usb-c host adapter, grant access, and
insert the disc.

## build

the build needs JDK 21, android platform 36, Build Tools 36.1.0,
NDK 29.0.14206865, and CMake 3.30.3.

```sh
scripts/build_android.sh debug
```

the script fetches the pinned xemu, SDL2, and libusb sources and writes the apk
under `../gdox-output/release/android`.

release builds need an external signing configuration and a complete
corresponding-source archive:

```sh
make android-source
```

## license

the combined android app is GPL-2.0. every public apk must include the exact
source used to build it.
