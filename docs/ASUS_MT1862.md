# ASUS DRW-24D5MT 2.00 experimental interface

GDOX includes a Windows native SATA profile for the exact
SCSI identity `ASUS DRW-24D5MT 2.00`. It currently accepts only the XGD2 Wave 2
media geometry. Original Xbox, XGD2 Wave 1, XGD3, other firmware revisions,
USB bridges, and other host platforms are unsupported for this profile.

## Physical validation

On 2026-09-08 UTC, a user-connected Windows Drive Reporter endpoint was used
to probe this exact drive and read an Assassin's Creed disc. The visible
stock volume was `XGD2DVD_NTSC`; the game partition supplied a valid GDFX
descriptor, directory entries, and `default.xex` with title ID `555307D4`.

Both READ(10) and READ(12) returned the same game descriptor after activation.
Repeated 32-sector samples across the game partition matched. A contiguous
6 MiB read completed in 36.33 seconds through the diagnostic relay. This is
a remote transport measurement, not a native SATA throughput benchmark.

The compiled GDOX source implementation was then exercised through a remote
SCSI transport adapter: open succeeded, the source exposed 3,825,924 sectors,
and a 65-sector read matched an independently captured sample. Source abort
followed by close restored the drive; independent reads verified all six
changed bytes, four guard bytes, stock capacity, and readiness.

Windows storage enumeration, desktop launch, Xenia gameplay, and native
disconnect/reconnect behavior have not yet been physically validated. The
profile is experimental and does not extend the validated gameplay matrix.
The reporter supplied no USB identity, but did not attest the Windows bus
type. GDOX therefore independently requires Windows to report SATA, ATA, or
ATAPI and rejects unknown, RAID, and USB bus types.

## Exact volatile state

| Field | XDATA addresses | Stock bytes | Live bytes |
| --- | --- | --- | --- |
| Capacity | `84C2–84C4` | `03 0A A3` | `3D 61 03` |
| Layer geometry | `8B92–8B94` | `03 08 6F` | `20 33 9F` |
| Read-only guard | `8B95–8B98` | `00 FC F9 C3` | unchanged |

The common MediaTek F1/02 read and F1/01 volatile-write commands work on this
firmware, but the existing GP63, GP65, and SP80 address tables do not apply.
GDOX verifies exact identity, guard bytes, media geometry, and known memory
state before activation. Unknown states fail closed. Activation writes
geometry before capacity; restoration writes capacity before geometry.
Each triplet is written in ascending address order and read back. Failed
activation and source closure use the existing transactional restoration path.

READ CAPACITY changes from last LBA `00000AA3` to `003A6103`, with 2048-byte
sectors. READ DVD STRUCTURE continues reporting the physical disc's original
PFI after activation; its layer-0 geometry must not be mistaken for the live
RAM value.

Transfers are capped at 32 sectors. GDOX retains the current read speed and
uses manual tray handling, including during read recovery. No firmware,
EEPROM, or disc writes are part of this interface. Raw research captures and
remote-session details remain in the private workspace.
