# changes

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
