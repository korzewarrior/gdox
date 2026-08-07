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

GDOX observes an idle drive without claiming its USB interface. It initializes
only after media becomes stable. An unidentified transport failure permits at
most two automatic retries, each after a fresh stability dwell. Invalid media,
identified setup failures, and exhausted retries remain latched until Start or
confirmed removal. Enumeration errors do not count as removal and cannot reset
the latch.

Managed Xenia launches disable persistent shader, instruction, guest-cache, and
scratch data. On Linux, storage, cache, logs, non-save content, and the Proton
prefix are scoped to a verified memory filesystem. The child temporary-file
directory also points into that session. On
Windows, the same derived data uses a GDOX-owned temporary session
because Windows has no driverless memory filesystem. Both session types are
removed on normal exit, failed launch, forced stop, and next-launch recovery.
Persistent Xenia content is limited to the saved-game and profile types
recognized by the exact patched runtime. Any unpatched or changed runtime hash
fails closed.

Recovery deletes only the legacy GDOX-owned Xenia `storage`, `proton`, and
`logs` roots. These contain derived runtime state, old Proton prefixes, and
logs. The separate `content` root is never removed wholesale: only content
types other than saved games (`00000001`), profiles (`00010000`), and Original
Xbox saves (`00060000`) are removed after a strict layout check. Unknown or
malformed layouts fail closed and remain untouched.

Managed xemu launches use the clean HDD in the release as an immutable backing
image. Guest HDD changes exist only in memory. The GDOX-owned durable vault holds
the fixed HDD configuration area, logical E:\UDATA, and only positively
reviewed E:\TDATA save paths. GDOX does not persist raw partitions, unreviewed
TDATA, installed games, title updates, DLC, or the X, Y, and Z scratch
partitions. Vault replacement uses two atomic slots; interruption retains the
previous complete generation.

An older GDOX-managed HDD is migrated only from its fixed historical path.
GDOX records its exact file identity, requires xemu to attest the same read-only
source, and validates a one-pass projection and round trip. A durable receipt
avoids repeating that full migration on later launches. Exact unchanged-file
deletion is allowed only after a fresh proof shows complete source projection
and finds no unclassified TDATA. A differing same-path save or configuration
entry remains in the current vault; nonconflicting saves are merged, playback
continues, and the old HDD is preserved. An explicitly rejected migration may
continue only while the same-size ordinary source remains and any existing
save-vault generation passes independent validation; an empty vault starts
clean. A malformed proof, failed validation, source-size change, symlink, or
ambiguous source fails closed. Unclassified title data preserves the source
and permits playback. POSIX deletion requires a private source and parent,
an exclusive lock, post-hash identity revalidation, and a durable quarantine
rename. Exactly one valid crash quarantine may be restored non-destructively;
fresh migration proof remains mandatory before removal. Files outside the
managed path are not migration inputs. GDOX does not delete data from
standalone xemu profiles.

This boundary covers files created or controlled by GDOX and its managed
emulators. An operating system or graphics driver may still use a global shader
cache, page file, swap, crash dump, or hibernation image. GDOX disables the
documented child-process caches it can control, but it does not claim authority
over those host-wide facilities.

During live play, GDOX polls readiness only through the owned source, serialized
with sector reads. Nonblocking MMC event status detects the drive's physical
eject button without allowing a queued pre-session event to affect a new
session. No-medium and medium-change sense data are latched as a new media
generation, so the old export cannot continue against a replacement disc and a
stale eject request cannot eject that replacement. Passive device presence is
checked separately; only successful repeated not-connected observations count
as a disconnect. Enumeration errors are unknown and never count as removal.
Either confirmed condition stops playback, closes the export, releases the
drive, and forces full media identification.

A physical eject request is completed only after playback, the export, and the
owned source have stopped. GP63, GP65, and GP08 then use their validated eject
command. ASUS remains manual: GDOX restores and releases the source, reports
that manual eject is required, and sends no tray command.

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
