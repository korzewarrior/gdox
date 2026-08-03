# Third-party notices

GDOX's original code and documentation are dedicated under the repository's
CC0 1.0 Universal `LICENSE`. Third-party works retain their own licenses.

The desktop application embeds or links the applicable components listed in
`packaging/licenses`; their notices are copied into release archives. Linux
uses the host OpenSSL library for hashing. Windows and macOS use platform
cryptography APIs.

Standard desktop release archives also contain a reviewed GDOX-patched xemu
executable as a separate program and a redistributable blank HDD from
xemu-dashboard. Their
versions, source URLs, hashes, sizes, and licenses are fixed in
`packaging/runtime-manifest.json`; the maintained integration patch series is
in `packaging/xemu/`. The exact corresponding xemu source archive is published
alongside GDOX binary releases.

Windows, Linux, and Steam Deck archives also contain exact, reviewed
GDOX-patched Xenia Canary executables as separate programs. Their commits,
upstream source and release URLs, integration patches, downstream archive
origins, hashes, sizes, and supported targets are fixed in
`packaging/runtime-manifest.json`. Each executable retains the upstream BSD
3-Clause license in its runtime directory. GDOX is not affiliated with or
endorsed by the Xenia project.

The Steam Deck archive also contains unmodified `nbdfuse` and `libnbd.so.0`
files from the pinned SteamOS libnbd package. They run as a separate read-only
bridge under libnbd's LGPL 2.1-or-later license. The exact upstream source,
Arch packaging commit and PKGBUILD, SteamOS package, and extracted-file
digests are fixed in `packaging/runtime-manifest.json`. GDOX does not
redistribute `fusermount3`; it uses the helper supplied by SteamOS.

GDOX does not distribute Microsoft MCPX data, Xbox BIOS data, EEPROM data,
keys, game data, optical-drive firmware, or security-sector binaries.

The Android application is based on a pinned official xemu revision and links
GDOX's physical-disc block source into that emulator. Its Android portability
layer records HakuX provenance in `android/dependencies.lock`. The combined APK
is a GPL-2.0 derivative. Android binary releases must be accompanied by the
exact patched xemu and SDL2 source, GDOX source, native dependency revisions,
build scripts, and applicable third-party notices.
