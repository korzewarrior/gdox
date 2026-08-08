# status

this page records physical validation and exact drive support.

## current systems

original Xbox and Xbox 360 are supported. the exact Xbox 360 platform, drive,
and media matrix is maintained in `XBOX360.md`.

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

## drive profiles

the physical adapters support four exact profiles:

| identity | current validation |
|---|---|
| `HL-DT-ST DVDRAM GP63EX70 RF02`, USB `0e8d:1887` | live play validated |
| `HL-DT-ST DVDRAM GP65NB60 PB00`, USB `0e8d:1887` | live play and restoration validated on windows 11 with the stock driver |
| `HL-DT-ST DVDRAM GP08NU10 JE01`, Prolific PL-2507 USB `152e:2507` | find, volatile activation, and xbox-sector read confirmed externally |
| `ASUS SDRW-08D1S-U A202`, Initio USB `13fd:1640` | original Xbox live play and restoration validated on windows 11 with the stock driver |

similar retail names, firmware revisions, bridges, and internal mechanisms are
not implied compatible. `GP65NB60 PB01` is not supported. other drives need
separate adapters.

Xbox 360 physical validation is narrower:

| identity | media | current validation |
|---|---|---|
| `HL-DT-ST DVDRAM GP63EX70 RF02`, USB `0e8d:1887` | XGD2 Wave 1, XGD2 Wave 2, and XGD3 | controllable gameplay validated on the host combinations in `XBOX360.md` |
| `ASUS SDRW-08D1S-U A202`, Initio USB `13fd:1640` | XGD2 | reader, sustained stream, rendered startup, and restoration validated on Linux through Proton; stable tested-title execution not established |

## preservation

a full-size image is not automatically redump-complete. gdox reports hashes,
unreadable sectors, and available security evidence separately. the stock
gp63 cannot create signed challenge data the drive did not return.
