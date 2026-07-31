# ASUS A202 / NR09 adapter

This adapter supports one exact stock-drive profile:

- USB `13fd:1640`;
- SCSI vendor `ASUS`;
- SCSI product `SDRW-08D1S-U`;
- SCSI revision `A202`.

The tested enclosure reported USB revision `0965` and an Initio
INIC-1610L bridge. Similar retail names, mechanisms, bridges, or firmware
revisions are not implied compatible.

## Transport

The adapter uses the operating system's standard optical transport. On
Windows 11 it runs through the stock Microsoft CD-ROM and USB mass-storage
drivers; no replacement driver is required.

The firmware exposes four-byte volatile-memory reads and writes through
vendor command `0xF1`. GDOX does not flash firmware. A power cycle clears the
temporary state.

## State transaction

Before writing, GDOX verifies all eight mutable fields, two neighboring fixed
fields, `READ CAPACITY(10)`, the 2,048-byte block size, and the stock PFI
prefix. Every value must exactly match the validated stock state.

| address | bank | stock | active |
|---|---:|---|---|
| `0x888da044` | `0x02` | `af 1a 03 00` | `af 33 20 00` |
| `0x1750` | `0x01` | `af 1a 03 00` | `af 33 20 00` |
| `0x18f4` | `0x01` | `b0 1a 03 00` | `b0 33 20 00` |
| `0x18f8` | `0x01` | `50 e5 fc 00` | `50 cc df 00` |
| `0x1900` | `0x01` | `af 1a 03 00` | `af 33 20 00` |
| `0x1908` | `0x01` | `af 1a 03 00` | `af 33 20 00` |
| `0x19cc` | `0x01` | `50 1b 03 00` | `50 4d 3d 00` |
| `0x1600` | `0x01` | `af 1a 03 00` | `af 33 20 00` |

The fixed fields must remain:

- `0x18fc`, bank `0x01`: `00 00 03 00`;
- `0x1904`, bank `0x01`: `10 1a 03 00`.

The stock last LBA is `0x1b4f`; the active last LBA is `0x3a4d4f`. The stock
PFI endpoint prefix is `03 1a af`.

Activation writes the seven geometry fields first and the capacity field
last, then reads the complete state back. Restoration writes the capacity
field first, restores the remaining fields, and verifies the complete original
state. Initialization, validation, read recovery, normal shutdown, and failed
shutdown all use the same restoration contract. If transport loss prevents
verification, GDOX asks for a drive power cycle.

## Disc validation and reads

After activation, GDOX reads physical LBA `0x30620` and requires the XDVDFS
magic at both ends of the sector. General reads use `READ(10)` and are split
into commands of at most 32 sectors, or 64 KiB.

Manual tray handling is required. GDOX never sends a load or eject command to
this mechanism.

The tested A202 drive streamed an original Xbox disc through the ordinary
compact-XISO and read-only NBD path into xemu on Arch Linux. The session
included sustained high-LBA reads and was followed by an independent stock
state verification on Windows 11.
