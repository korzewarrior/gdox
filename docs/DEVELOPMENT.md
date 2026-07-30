# Development

## Toolchain

- CMake 3.25 or newer;
- Ninja;
- a C17 and C++20 compiler;
- Python 3.11 or newer for release tooling;
- libusb and OpenSSL development files on Linux.

Configure, build, and test:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev --output-on-failure
```

The portable core can be checked without the UI or optical layer:

```sh
cmake --preset core
cmake --build --preset core --parallel
ctest --preset core --output-on-failure
```

`make check` runs both configurations, script syntax checks, the privacy audit,
and whitespace validation.

## Source layout

```text
include/gdox/       stable C contracts
src/core/           portable algorithms and data models
src/app/            façade, session runtime, controls, preservation, preferences
src/platform/       operating-system and optical transports
src/ui/             raylib/Dear ImGui presentation
tests/              deterministic unit and integration fixtures
cmake/              one module per build concern
packaging/          shipping launchers, installers, runtime manifest
scripts/            audited build and release tools
```

Keep platform conditions in `src/platform` or CMake source selection. Avoid
preprocessor branches inside algorithms when a backend interface is
appropriate. Prefer explicit ownership, fixed-capacity path contracts at C
boundaries, and fail-closed validation for disc geometry and device identity.

## Release builds

```sh
python scripts/build_linux_packages.py --version 0.1.2
python scripts/build_release.py --target aarch64-apple-darwin
```

Generated build trees and packages default to the sibling `gdox-output`
directory. Set `GDOX_OUTPUT_ROOT` to select another location. Verified
third-party downloads use the platform cache directory, or
`GDOX_CACHE_ROOT` when set.

MSVC builds use:

```bat
scripts\build_msvc.cmd "%CD%" "%CD%\..\gdox-output\build\x86_64-pc-windows-msvc"
```

Every release build starts from an empty target directory unless `--reuse` is
specified, enables warnings as errors, runs tests, remaps build paths, and
audits the executable.

## Tests

Tests cover source bounds, XDVDFS parsing, compact layout, preservation
atomicity and manifests, hashing, NBD negotiation and invalid clients,
preferences, runtime-bundle discovery, title-aware output names, sessions,
security-map validation, and the optical monitor's stable-media and
failure-latching rules.

Physical validation is separate from deterministic tests. Record the host OS,
drive model/revision, USB ID, disc mastering, operation, sustained duration,
eject behavior, and restoration result. Never infer support for another
mechanism from an enclosure label.
