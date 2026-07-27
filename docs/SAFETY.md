# Safety

## Normal use

Live play is read-only with respect to the disc and game data. The GP63 adapter
uses a narrow, validated volatile memory transaction; it does not flash drive
firmware. Normal teardown restores the prior values, and a USB power cycle
clears volatile state if the process is terminated unexpectedly.

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
drive adapter runs. GDOX fails closed for unknown hardware.

## Private data

Firmware paths, user paths, xemu logs, and preservation manifests can identify
a machine or a disc. Review them before sharing. Never publish MCPX, Xbox BIOS,
EEPROM, keys, game images, or third-party security-sector binaries in a bug
report or source repository.

## Legal use

GDOX contains no Microsoft ROMs, keys, or game content. Users are responsible
for supplying their own prerequisites and for using discs and preserved data
in accordance with applicable law.
