# Disc preservation

GDOX creates two layouts that both conventionally use the `.iso` extension.

## Playable XISO

A playable XISO contains the XDVDFS game filesystem without the DVD-video
area, security sectors, and unused layout padding. GDOX enumerates the source,
rejects unsafe or ambiguous layouts, and rebuilds referenced directories and
files into a new aligned image. It is normally much smaller than the physical
disc and is intended to boot directly in xemu.

Where required for common homebrew-capable BIOS configurations, GDOX applies
the established one-byte media compatibility change to affected XBE copies in
the new XISO. The physical disc and full-disc output are never modified.

## Full-disc preservation

An original-Xbox full-disc logical image has 3,820,880 sectors of 2,048 bytes,
or 7,825,162,240 bytes. It retains the whole logical layout and available
PFI, DMI, security, layer-break, read-error, and normalization evidence. This
layout is for archival work and is not a direct xemu disc layout. GDOX can
open it as an explicit read-only image source, locate the game partition, and
present xemu with a compatible in-memory view without changing the archive.

The manifest distinguishes:

- **evidence complete**: required structures and authenticated security ranges
  are present, and no unexpected sectors were unreadable;
- **candidate**: the fixed logical image exists, but some submission evidence
  is unavailable;
- **canonical hash match**: available published hashes match the normalized
  bytes.

These are independent claims. A correct file size or catalog hash does not
create missing signed security evidence, and database acceptance remains a
community decision.

## Hashing and atomic output

GDOX computes CRC32, MD5, SHA-1, and SHA-256 while writing. With verification
enabled, it reopens the result and requires a second complete hash pass to
match. Output is written to a `.part` path, never overwrites an existing file,
and receives the final name only after successful finalization.

Cancelled or failed partial data is not a playable or archival result.
Unexpected unreadable sectors are zero-filled for deterministic geometry and
recorded separately; repeat the dump after cleaning the disc.

## Security maps and `SS.bin`

An XGD1 security map identifies the 16 protected 4,096-sector ranges used for
normalization. GDOX prefers a structurally valid range table from authentic
security evidence, then may use an exact geometry/PFI/DMI catalog match.

`SS.bin` also contains disc-specific challenge data, hashes, and signatures.
Those bytes cannot be derived from game files or replaced with a generic
template. The stock GP63 path does not manufacture or download them. A
catalog-derived range map remains explicitly unauthenticated.

Keep the image, manifest, log, `.dvd` descriptor, and every emitted evidence
sidecar together. For archival confidence, dump a clean disc independently at
least twice and compare hashes.
