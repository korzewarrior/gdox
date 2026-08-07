# changes

## 0.2.1

- allow playback after a rejected legacy Xbox hard-disk migration when the
  same-size source remains and saved games validate
- make the included clean Xbox hard disk and separate save storage clear in the
  interface
- simplify public package names and replace duplicate checksum files with one
  signed checksum manifest
- publish GDOX, xemu, and libnbd corresponding source in one archive
- document the Linux/Proton ASUS A202 XGD2 validation boundary

## 0.2.0

- add Xbox 360 image playback on Windows x86-64, Linux x86-64, and Steam Deck
- add Xbox 360 physical playback through the exact GP63EX70/RF02 profile: XGD2
  on Windows, Linux, and Steam Deck; XGD3 on Linux and Steam Deck
- keep automatic disc detection available from the Windows notification area,
  macOS menu bar, and compatible Linux desktops
- preserve Original Xbox and Xbox 360 saves and profiles without keeping game
  content, shader caches, or session files
- improve Xenia title compatibility and Steam Deck performance
- harden disc switching, emulator shutdown, storage migration, runtime
  verification, and release packaging

## 0.1.4

- stop background controller input from activating gdox navigation, links, or
  quit while xemu is running; controller navigation now resumes only after
  gdox regains focus and every button has been released
- simplify internal session commands, desktop state, optical discovery, build
  layers, version metadata, and platform tests without changing supported
  hardware

## 0.1.3

- exact `ASUS SDRW-08D1S-U A202` support for original Xbox discs through the
  stock Windows optical driver
- verified volatile activation, high-LBA XDVDFS reads, live play, and complete
  restoration on the tested Initio `13fd:1640` USB bridge
- ASUS reads are limited to the validated 32-sector transfer size
- ASUS tray handling is manual; GDOX never sends load or eject commands to this
  mechanism

## 0.1.2

- exact `HL-DT-ST DVDRAM GP65NB60 PB00` support, including guarded recovery of the
  known auxiliary-state corruption left by older Drive Reporter builds
- verified PB00 activation, XDVDFS reads, live play, and restoration on
  windows with the stock optical driver
- PB00 reads on windows are limited to the validated 32-sector transfer size
- restrained desktop high-dpi scaling for 4k displays

## 0.1.1

- exact `HL-DT-ST GP08NU10 JE01` profile behind the Prolific PL-2507 USB
  bridge, with guarded volatile activation and restoration

## 0.1.0

first public development release.

- live original xbox disc playback through the stock
  `HL-DT-ST GP63EX70 RF02`
- playable xiso and full-disc preservation with verification, hashes, and
  evidence files
- read-only playback from preserved images
- linux, steam deck, macos, and windows packages with matching xemu builds
- steam deck gaming mode install and controller handoff
- android arm64 test build with the disc reader inside QEMU
- bounded optical recovery, clean disconnect handling, and exact read errors
- private firmware, hdd, settings, saves, and preservation output
- deterministic tests, privacy checks, and reproducible package tools
