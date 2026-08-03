# changes

## 0.2.0

- add reviewed, storage-isolated Xbox 360 image playback on Windows x86-64,
  Linux x86-64, and Steam Deck
- add exact-profile GP63EX70/RF02 Xbox 360 physical playback for XGD2 on
  Windows, Linux, and Steam Deck and XGD3 on Linux and Steam Deck, with bounded
  read-only access and exact drive restoration
- validate GDFX and XEX identity before launch, select the reviewed per-title
  policy, isolate managed state, and disable rich presence
- add a Steam Deck performance profile while keeping title compatibility and
  desktop rendering policy independent
- keep disc monitoring and automatic playback available from the Windows
  notification area, macOS menu bar, and compatible Linux desktop hosts
- keep Original Xbox guest writes behind a volatile HDD layer and disable
  persistent emulator shader caches; persist HDD configuration and profiles,
  the logical E:\UDATA save tree, and only explicitly reviewed E:\TDATA save
  paths through an atomic vault separate from the hard-disk image
- align the Android arm64 physical-disc path with the volatile HDD and managed
  save-vault boundary while keeping the Android package in development
- migrate only the fixed historical GDOX-managed xemu HDD through a read-only,
  independently verified save projection; preserve the source on any mismatch
  and remove the obsolete custom-HDD runtime setting
- give xemu an orderly 15-second save-checkpoint window during session teardown
  and leave standalone xemu profiles and caches untouched
- isolate managed emulator processes from inherited Steam loader state so
  capability checks, firmware detection, and disc swaps remain reliable
- improve Xenia title compatibility, texture handling, and Steam Deck
  compositor integration
- add save-only persistent-content isolation for managed Xenia runtimes;
  Linux transient state, temporary files, and the Proton prefix use a verified
  memory filesystem, and ineligible runtimes or hosts fail closed
- remove legacy GDOX-owned Xenia storage, Proton-prefix, and log trees during
  recovery while retaining only validated save and profile content types
- harden physical-media recovery, process teardown, runtime integrity checks,
  image compaction, and release validation

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
