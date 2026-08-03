# status

gdox is an active development release. a build can prove that the code
runs; only a disc and drive can prove the physical path.

## current systems

original Xbox and Xbox 360 are the 0.2 systems. the Xbox 360 target is windows
x86-64, linux x86-64, and steam deck. the two required patched Xenia revisions
are pinned and pass their capability, storage, and archive checks. physical
Xbox 360 support is limited to the exact GP63EX70/RF02 profiles in
`XBOX360.md`. macos and android have no Xenia integration.

## host platforms

| platform | original Xbox current state |
|---|---|
| linux x86-64 | live play validated on arch linux |
| steam deck | live play validated in gaming mode |
| macos apple silicon | live play validated |
| macos intel | image playback |
| windows 11 x86-64 | live path validated with the stock driver |
| android arm64 | physical playback reaches the game menu on android 16; not released |

for Original Xbox, the desktop app can play from a disc or preserved image and
make a playable xiso or full-disc image. Firmware, EEPROM, and settings live
outside the application folder. The 0.2 implementation keeps the complete guest
HDD volatile and permits only its fixed configuration/profile area, logical
E:\UDATA, and positively reviewed E:\TDATA save paths to cross that boundary.
Game contents and emulator scratch are not persistent. Xbox 360 preservation
is disabled.

the desktop implementation has fail-closed migration orchestration for the fixed
historical GDOX-managed xemu HDD: source attestation, no-boot read-only
projection, independent vault validation, and exact unchanged-source deletion.
the desktop package gates cover repeat launch, save projection, migration,
recovery, and process teardown. android remains blocked until its packaged
runtime and managed-storage transition pass the equivalent save-boundary tests.

the Xbox 360 integration validates GDFX and XEX identity, selects a reviewed
title policy, verifies pinned Xenia executables, and isolates state by runtime.
the patched runtimes persist only saved-game and profile content. linux and
steam deck keep transient state in a verified memory filesystem and run the
reviewed Windows builds through Proton Experimental. windows keeps transient
state in a GDOX-owned session directory and removes it on normal exit, failed
launch, forced stop, and next-launch recovery.

linux and steam deck implement the exact GP63EX70/RF02 XGD2 and XGD3 physical
profiles. windows implements image playback and the exact GP63EX70/RF02 XGD2
physical profile. current testing covers identification, guest launch, live
rendering, clean teardown, eject, and switching between Original Xbox and Xbox
360 media on Windows 11, Arch Linux, and Steam Deck. the exact platform matrix
and runtime contract are in `XBOX360.md`.

## Xbox 360 release evidence

the reviewed runtime identities, storage capability probes, empty-cache
packages, physical playback, teardown, and media-transition checks are complete
for the current Windows, Arch Linux, and Steam Deck hosts. broader title,
hardware, and long-session testing continues as normal development work. the
repeatable publication checklist is maintained in `RELEASING.md`.

## drive profiles

the physical adapters support four exact profiles:

| identity | current validation |
|---|---|
| `HL-DT-ST DVDRAM GP63EX70 RF02`, USB `0e8d:1887` | live play validated |
| `HL-DT-ST DVDRAM GP65NB60 PB00`, USB `0e8d:1887` | live play and restoration validated on windows 11 with the stock driver |
| `HL-DT-ST DVDRAM GP08NU10 JE01`, Prolific PL-2507 USB `152e:2507` | an external report confirmed the find, volatile activation, and expected xbox-sector read; project-side live-play testing remains |
| `ASUS SDRW-08D1S-U A202`, Initio USB `13fd:1640` | original Xbox live play and restoration validated on windows 11 with the stock driver |

similar retail names, firmware revisions, bridges, and internal mechanisms are
not implied compatible. `GP65NB60 PB01` is not supported. other drives need
separate adapters.

## still needed

- more gp63 units and more clean and scratched discs
- physical gp65 testing on linux and macos
- project-side physical and live-play validation of the gp08 profile
- physical ASUS testing on linux and macos, plus a second A202 unit
- long play sessions across protected ranges
- repeated suspend, reset, eject, disconnect, and forced-exit testing
- physical testing on an intel mac
- longer windows, steam deck, and android runs
- signed macos and windows releases

## preservation

a full-size image is not automatically redump-complete. gdox reports hashes,
unreadable sectors, and available security evidence separately. the stock
gp63 cannot create signed challenge data the drive did not return.

## 1.0

extract one package, complete at most one device-access step, add your own
firmware, insert a disc, and play or preserve it without a terminal.
