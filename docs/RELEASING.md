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

Build and sign an Android validation candidate on the pinned Android
toolchain:

```sh
export GDOX_ANDROID_KEY_PROPERTIES=/secure/path/gdox-key.properties
scripts/build_android.sh release
```

The signing properties and keystore must remain outside the repository and
generated build trees. `storeFile` must be an absolute path.
Android candidates are not part of the public tag workflow while the packaged
runtime and managed-storage transition remain release gates.

## Package

```sh
python scripts/package_release.py \
  --target x86_64-unknown-linux-gnu \
  --artifact ../gdox-output/build/x86_64-unknown-linux-gnu/gdox
```

The release auditor treats `--path` and `--artifact` as explicit inputs.
Artifact-only invocations inspect only those artifacts. With neither option it
audits the repository; use `--path .` when combining a source-tree audit with
one or more artifacts.

`scripts/build_linux_packages.py` and `packaging/linux/Containerfile` are the
supported Linux and Steam Deck build path. They use a digest-pinned Ubuntu
userspace so bundled libusb does not depend on the developer workstation. Both
targets share one executable compatibility contract: the GDOX ELF may require
glibc symbols no newer than `GLIBC_2.38`. Build, package, and release-audit
entry points parse the ELF dynamic version requirements directly and reject a
newer host-built artifact before staging it. Raising this ceiling requires an
explicit platform policy change and validation on every supported Linux
target. Package creation fetches only hash-pinned runtime assets, checks the
expected layout, audits the staged tree and binary, creates a deterministic
archive, audits the archive, and writes SHA-256.

The runtime manifest also pins each extracted xemu notice file by member name,
size, and SHA-256. Only an exact match may suppress personal-email findings
from an upstream copyright notice. Substituted files and lookalike paths are
audited as GDOX release content.

Upstream xemu binaries with embedded build paths or contact addresses are also
pinned by member name, size, and SHA-256. Those exact files may suppress only
path and email findings. The exact pinned libgnutls runtime may additionally
suppress its upstream self-test private-key finding; credential scanning stays
enabled. Any byte change removes these exemptions.

Xenia runtime revisions are read from `packaging/runtime-manifest.json`; release
tooling must not carry a separate revision list. Validate manifest structure,
compatibility cross-links, and the generated policy with:

```sh
python scripts/fetch_runtime.py validate
python scripts/generate_xenia_policy.py --check
```

This validation is intentionally offline. It accepts a patched runtime marked
`candidate-only` so its reviewed binary and build provenance can be tested, but
that state is not publishable. The release workflow additionally runs:

```sh
python scripts/fetch_runtime.py publishable --target x86_64-unknown-linux-gnu
python scripts/fetch_runtime.py publishable --target x86_64-pc-windows-msvc
python scripts/fetch_runtime.py publishable --target x86_64-apple-darwin
python scripts/fetch_runtime.py publishable --target aarch64-apple-darwin
```

The publishable gate also rejects xemu artifacts without the volatile full-HDD
contract and Xenia artifacts without save-only persistent content. Candidate
source patches do not satisfy this gate; the exact rebuilt executables must be
pinned in the runtime metadata and admitted by the separate code-reviewed
artifact-digest gates after exact capability testing. Manifest flags alone
cannot make an emulator publishable.

Build patched xemu from the extracted pinned source archive without a `.git`
directory so `XEMU_VERSION` and `XEMU_COMMIT` remain the authoritative source
identity. Map the absolute extraction path to the canonical source path so
assertion strings and build IDs do not depend on the build directory. Set the
same fixed epoch for configuration and compilation:

```sh
export SOURCE_DATE_EPOCH=315532800
xemu_source_root=$(pwd -P)
xemu_prefix_map="-ffile-prefix-map=${xemu_source_root}=/usr/src/xemu-0.8.136"
mkdir build
cd build
../configure \
  --extra-cflags="-DXBOX=1 -Wno-error=redundant-decls ${xemu_prefix_map}" \
  --extra-ldflags= \
  --target-list=i386-softmmu \
  --disable-werror \
  -Db_lto=true \
  -Dx86_version=3
ninja
strip --strip-unneeded qemu-system-i386
```

The patched version generator rejects an empty, negative, nonnumeric, or
unsupported epoch. Omitting the variable retains xemu's ordinary current-time
build stamp and is therefore not permitted for a release artifact.

Build the universal macOS runtime only with the pinned native recipe. It
requires empty, separate work and output trees, the pinned xemu and
dylibbundler source archives, the toolchain recorded in the integration
metadata, and Rosetta for the Intel audit:

```sh
python packaging/xemu/build_macos.py \
  --source-archive /absolute/path/xemu-0.8.136.tar.zst \
  --dylibbundler-source-archive \
    /absolute/path/dylibbundler-1.0.5.tar.gz \
  --work-root /absolute/generated/xemu-macos-work \
  --output-directory /absolute/generated/xemu-macos-output
```

The recipe applies the six reviewed patches in separate Apple Silicon and
Intel source trees, pins each deployment target in both the compiler and
linker arguments, normalizes the known duplicate `LC_RPATH`, rejects any other
runtime layout, runs both storage suites including raw/QCOW2 migration, probes
the exact capability response natively and through Rosetta, assembles and
ad-hoc signs the universal app, and writes the deterministic archive. A
different executable or archive identity fails the build.

The xemu gate must also exercise legacy managed-HDD migration without booting:
same-handle source size and SHA-256 verification, read-only source access,
projection against the pinned clean HDD, vault reopen and round-trip
validation, and exact unchanged-source deletion. Test malformed output,
timeouts, source mutation, symlinks, conflicting existing vaults, and nonzero
helper exits; each must preserve the source. Android remains non-publishable
until its packaged runtime and prior managed-storage path satisfy the same
logical-save boundary.

Before release, package Linux and Windows from an empty cache. Packaging
downloads each exact upstream or downstream archive, checks its size and
SHA-256, permits only the expected executable and license, then verifies the
extracted executable size and SHA-256 independently. A patched Windows runtime
must identify its actual downstream archive; substituting the upstream release
URL is rejected because it cannot contain the reviewed executable.

Windows, Linux, and Steam Deck packages must contain every Xenia revision
declared for their runtime target. Linux launchers must be executable and must
retain their embedded executable size and digest checks. macOS packages must
not contain Xenia. Release packages must not contain research, website, or
workspace material.

Create the source archives with:

```sh
python scripts/package_source.py
python scripts/fetch_runtime.py source --output ../gdox-output/release
python scripts/package_android_source.py
```

The separate xemu archive satisfies corresponding-source distribution for the
bundled desktop executable. The source job also publishes the exact libnbd
source archive and the exact Arch PKGBUILD used for the Steam Deck bridge.
Their recorded SHA-256 digests must match the SteamOS package build metadata.
The GDOX source archive contains the exact Xenia integration patches and pinned
Windows build recipe referenced by the runtime manifest. Xenia's BSD 3-Clause
license is retained beside every staged executable. The Android archive
contains the exact patched xemu and SDL2 trees plus every native source used by
the APK. Its component set is fixed by the source packager and shared with the
release auditor; a missing, extra, duplicated, or self-declared component makes
the corresponding-source package invalid.

## Signing

The automated desktop release is ad-hoc signed on macOS and unsigned on
Windows, matching the warnings on the download pages. To publish a notarized
macOS build, set `GDOX_CODESIGN_IDENTITY` to a Developer ID identity and
notarize and staple it outside the generic packager. Apply Authenticode to a
Windows build after the audited MSVC build and before the final package audit.

The macOS packager signs the GDOX executable and outer application envelope
explicitly. It does not deep re-sign the bundled xemu application. The pinned
xemu executable identity is verified before and after signing so its upstream
signature and reviewed bytes remain unchanged.

## Publication

The reviewed downstream emulator assets are a separate bootstrap release whose
name is `runtime-v` followed by the project version; it deliberately does not
match the `v*` application release trigger. Its exact inventory includes the
pinned xemu corresponding-source archive whenever patched xemu binaries are
present. The runtime tag points at the commit containing the exact patch series
and build recipes. Assemble only the assets returned by the runtime inventory
and audit the directory before upload:

```sh
release_version=$(python scripts/project_version.py)
runtime_release="runtime-v${release_version}"
python scripts/fetch_runtime.py audit-runtime-release \
  --path "../gdox-output/release/${runtime_release}"
```

After the final `main` commit is identical on GitHub and Forgejo, create the
GitHub runtime release at that commit and upload the audited directory
without renaming or replacing any file. Verify every HTTPS URL from an empty
cache by packaging each desktop target. A missing URL, changed byte, extra
file, or checksum mismatch stops publication. Only then create the versioned
`v*` application tag. Do not reuse the runtime release name for later bytes; a
runtime change requires a new manifest identity and release name.

Tags matching `v*` build the platform matrix and attach binary packages, the
Steam Deck package, GDOX source, xemu and libnbd corresponding source, the
libnbd build recipe, and checksums.
The publish job writes a combined `SHA256SUMS` and signs it with minisign
using the `MINISIGN_SECRET_KEY` repository secret. The public key is
`packaging/minisign.pub`, mirrored at `https://gdox.korze.org/minisign.pub`;
verify with:

```sh
minisign -V -p minisign.pub -m SHA256SUMS
sha256sum -c SHA256SUMS
```

The GitHub workflow creates or updates a draft. It extracts the exact version
section from `CHANGELOG.md` and rejects an absent, empty, duplicate, or
unreleased section; commit-history text is not used as release copy. Keep that
release unpublished until the final distribution promotion is ready. The
tagged commit must equal the current `origin/main` commit; the workflow verifies
that identity before building. Promote only that reviewed `main` commit and
release tag to GitHub and Forgejo, attach the same release notes and asset set
to both, and compare the asset names, byte lengths, `SHA256SUMS`, and
`SHA256SUMS.minisig` before publication. Neither remote may expose a private
branch.

The website is a separate repository and deployment. After the two release
mirrors agree, update its release status, system matrix, platform pages, asset
links, and displayed sizes from the reviewed artifacts; copy those exact
artifacts into the versioned download directory; then deploy and verify every
sitemap URL plus the live checksum and signature bytes. A mismatch leaves the
release unpublished.

Publish a build only after the physical validation matrix in `STATUS.md`
matches its claims.

Xbox 360 physical support remains limited to the explicitly validated host,
drive, firmware, and media profile. Do not generalize one validation run to
another optical drive, firmware, media profile, or host platform.
The supported platform matrix is maintained in `XBOX360.md`.
