# Third-party notices

GDOX's original code and documentation are dedicated under the repository's
CC0 1.0 Universal `LICENSE`. Third-party works retain their own licenses.

The desktop application links or embeds pinned versions of raylib, Dear ImGui,
rlImGui, Native File Dialog Extended, libusb, OpenSSL or platform crypto APIs,
and platform system libraries. Their notices are retained in `packaging/licenses`
and copied into each release archive.

Release archives also contain an unmodified xemu executable as a separate
program and a redistributable blank HDD from xemu-dashboard. Their versions,
source URLs, hashes, sizes, and licenses are fixed in
`packaging/runtime-manifest.json`. The exact corresponding xemu source archive
is published alongside GDOX binary releases.

GDOX does not distribute Microsoft MCPX data, Xbox BIOS data, EEPROM data,
keys, game data, optical-drive firmware, or security-sector binaries.

The Android application is based on a pinned official xemu revision and links
GDOX's physical-disc block source into that emulator. Its Android portability
layer records HakuX provenance in `android/dependencies.lock`. The combined APK
is a GPL-2.0 derivative. Android binary releases must be accompanied by the
exact patched xemu and SDL2 source, GDOX source, native dependency revisions,
build scripts, and applicable third-party notices.
