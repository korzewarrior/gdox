# Releasing

## Gate

Start from a clean worktree and run:

```sh
make check
```

Build Linux and Steam Deck in the pinned compatibility image:

```sh
python scripts/build_linux_packages.py
```

Build Apple targets on their native host:

```sh
python scripts/build_release.py --target x86_64-apple-darwin
python scripts/build_release.py --target aarch64-apple-darwin
```

Use `scripts\build_msvc.cmd` in a Visual Studio environment for Windows.

Build and sign Android on the pinned Android toolchain:

```sh
export GDOX_ANDROID_KEY_PROPERTIES=/secure/path/gdox-key.properties
scripts/build_android.sh release
```

The signing properties and keystore must remain outside the repository and
generated build trees. `storeFile` must be an absolute path.

## Package

```sh
python scripts/package_release.py \
  --target x86_64-unknown-linux-gnu \
  --artifact ../gdox-output/build/x86_64-unknown-linux-gnu/gdox
```

The compatibility builder uses a digest-pinned Ubuntu userspace so its glibc
floor and bundled libusb do not depend on the developer workstation. Package
creation fetches only hash-pinned runtime assets, checks the expected layout,
audits the staged tree and binary, creates a deterministic archive, audits the
archive, and writes SHA-256.

Create the source archives with:

```sh
python scripts/package_source.py
python scripts/fetch_runtime.py source --output ../gdox-output/release
python scripts/package_android_source.py
```

The separate xemu archive satisfies corresponding-source distribution for the
bundled desktop executable. The Android archive contains the exact patched
xemu and SDL2 trees plus every native source used by the APK.

## Signing

The automated desktop release is ad-hoc signed on macOS and unsigned on
Windows, matching the warnings on the download pages. To publish a notarized
macOS build, set `GDOX_CODESIGN_IDENTITY` to a Developer ID identity and
notarize and staple it outside the generic packager. Apply Authenticode to a
Windows build after the audited MSVC build and before the final package audit.

## Publication

Tags matching `v*` build the platform matrix and attach binary packages, the
Steam Deck package, GDOX source, xemu corresponding source, and checksums.
The publish job writes a combined `SHA256SUMS` and signs it with minisign
using the `MINISIGN_SECRET_KEY` repository secret. The public key is
`packaging/minisign.pub`, mirrored at `https://gdox.korze.org/minisign.pub`;
verify with:

```sh
minisign -V -p minisign.pub -m SHA256SUMS
sha256sum -c SHA256SUMS
```

Publish a build only after the physical validation matrix in `STATUS.md`
matches its claims.
