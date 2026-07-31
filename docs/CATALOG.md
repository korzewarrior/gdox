# Original Xbox offline mastering catalog

`catalog/xgd1.json` is a small reviewed XGD1 catalog used for deterministic
full-disc normalization when a drive exposes PFI, DMI, and game data but not
authenticated decrypted security-sector evidence.

An entry matches only on exact logical sector count, PFI CRC32, and an accepted
DMI CRC32. Title text, filename, region labels, and filesystem hashes are
never selectors. Zero or multiple matches produce no automatic map.

Every map contains 16 sorted, non-overlapping ranges of 4,096 sectors each,
expressed as canonical zero-based ISO LBAs. The ranges must be in bounds and
total 65,536 sectors. The same validator applies to authenticated, catalog,
and explicit maps.

A catalog match can provide normalization ranges and published reference
hashes. It does not provide the signed challenge bytes from `SS.bin`, so the
manifest records:

```text
security map source: catalog-derived
SS authenticated: false
```

If every available reference hash matches, the manifest records that
separately from local readback verification and security authentication.

Catalog changes must include stable non-personal identifiers, exact geometry,
PFI/DMI selectors, mastering information, all ranges, reference hashes when
available, and provenance URLs. Run `make check` after changing the JSON or
embedded catalog. Do not bulk-import uncertain scraped data or include
third-party `SS.bin` files.
