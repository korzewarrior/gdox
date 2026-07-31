# Safety

## Normal use

Live play is read-only with respect to the disc and game data. The supported
drive adapters use narrow, validated volatile memory transactions; they do not
flash drive firmware. Normal teardown restores the prior values, and a USB
power cycle clears volatile state if the process is terminated unexpectedly.

The GP65, GP08, and ASUS adapters verify the exact USB and SCSI identity and
every expected stock value before activation. They run their complete restore
sequences on teardown and every error path, and report when a transport
failure makes a power cycle necessary.

The GP65 profile never applies the GP63 address table. It requires the PB00
auxiliary field to be canonical or to contain only the per-byte GP63
stock/live values left by older Drive Reporter builds. Known combinations are
restored to `64 00 64`; any other value is rejected without writing.

The ASUS profile writes only its eight validated volatile fields. It verifies
the two neighboring fixed fields before activation, writes the capacity field
last, restores the capacity field first, and reads the complete stock state
back after restoration. Its tray is manual-close; GDOX does not send load or
eject commands to this drive.

Preserve is the only path that writes game data. It writes to a user-selected
file, never to the disc or optical drive, and publishes the final name only
after successful finalization.

Do not run GDOX or xemu as root. On Linux, use the supplied udev rule. On
Windows, keep the standard optical-storage driver; no replacement device
driver is required.

GDOX observes an idle drive without claiming its USB interface. It makes one
active initialization attempt after media becomes stable. If that attempt
fails, automatic retry stops and Start is the only way to request another
attempt. Do not add polling that repeatedly detaches and reattaches the
operating-system mass-storage driver.

During live play, GDOX uses the platform's passive presence path where one is
available. A missing device ends xemu and the NBD session immediately; do not
rely on a later game read to discover removal because xemu and the emulated
console can retain already-read data in memory.

An unexplained hard reset is a stop condition. Disconnect the optical drive,
collect the previous boot's kernel journal, and do not reproduce the event
until the transport path has been reviewed. If a reset occurs without a
kernel panic or shutdown record, also test the drive through an independently
powered USB hub to separate software/USB-controller faults from a power
delivery fault.

## Hardware identity

Do not assume two retail enclosures contain the same optical mechanism.
Require the exact model, revision, and USB identity shown on Details before a
drive adapter runs. GP63 and GP65 share USB `0e8d:1887`, so the complete SCSI
identity selects the profile. The GP08 profile additionally requires the exact
Prolific PL-2507 USB bridge identity. The ASUS profile requires USB
`13fd:1640`, SCSI vendor `ASUS`, product `SDRW-08D1S-U`, and revision `A202`.
GDOX fails closed for unknown hardware or an unexpected stock memory state.

## Private data

Firmware paths, user paths, xemu logs, and preservation manifests can identify
a machine or a disc. Review them before sharing. Never publish MCPX, Xbox BIOS,
EEPROM, keys, game images, or third-party security-sector binaries in a bug
report or source repository.

## Legal use

GDOX contains no Microsoft ROMs, keys, or game content. Users are responsible
for supplying their own prerequisites and for using discs and preserved data
in accordance with applicable law.
