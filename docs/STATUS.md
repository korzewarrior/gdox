# status

gdox is an active development release. a build can prove that the code
runs; only a disc and drive can prove the physical path.

## current system

original xbox.

## host platforms

| platform | current state |
|---|---|
| linux x86-64 | live play validated on arch linux |
| steam deck | Halo and Morrowind boot in gaming mode |
| macos apple silicon | live play validated |
| macos intel | builds; physical drive not tested |
| windows 11 x86-64 | live path validated with the stock driver |
| android arm64 | Halo reaches the menu on android 16; not released |

the desktop app can play from a disc or preserved image, make a playable xiso
or full-disc image, and keep firmware, hdd data, settings, and saves outside
the application folder.

## drive profiles

the physical adapters support four exact profiles:

| identity | current validation |
|---|---|
| `HL-DT-ST DVDRAM GP63EX70 RF02`, USB `0e8d:1887` | live play validated |
| `HL-DT-ST DVDRAM GP65NB60 PB00`, USB `0e8d:1887` | live play and restoration validated on windows 11 with the stock driver |
| `HL-DT-ST DVDRAM GP08NU10 JE01`, Prolific PL-2507 USB `152e:2507` | Eddi (`@eddifpv`) reported the find, volatile activation, and expected xbox-sector read; project-side live-play testing remains |
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
