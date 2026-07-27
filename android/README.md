# GDOX for Android

The Android application is a native arm64 xemu port with GDOX's optical
source linked into QEMU as a read-only block protocol. It accepts only the
validated `0e8d:1887` GP63 path. There is no ISO picker, game library, or
fallback game image in the GDOX launch path.

Android grants the app temporary USB-host access. GDOX passes the resulting
file descriptor to libusb, performs the same GP63/MT1887 transaction as the
desktop application, and builds an in-memory compact XDVDFS layout. Directory
metadata is synthesized in memory. File data is read from the physical disc
when QEMU requests it. Disconnecting the drive closes the USB connection and
requests immediate emulator shutdown.

The Xbox HDD still provides the X, Y, and Z scratch partitions present on a
real console. GDOX preserves them after an orderly emulator exit or drive
disconnect so games can reuse their own temporary data, but they are not a
disc image and cannot start a game without the physical disc. A native crash
or unexpected process loss leaves a marker; GDOX then rebuilds the
scratch-partition metadata before the next boot.

Android arm64 uses an AAPCS64 coroutine context that is independent of
Bionic's thread-local pointer-authentication keys. This lets QEMU resume a
coroutine on another I/O thread without failing return-address authentication,
while retaining asynchronous raw HDD I/O. Optical data continues to be read
on demand from the physical disc.

While the launcher is visible, one passive command channel observes the drive
and confirms tray or media changes before updating the interface. **Eject**
opens the physical tray. Starting a game first releases that observer without
resetting or re-enumerating the USB device, then gives exclusive ownership to
the emulator. Removing media ends the live emulator session; the launcher then
waits for the dedicated emulator process to end, reacquires the drive, and
waits for the next disc. A failed native startup follows the same bounded
teardown path instead of leaving the launcher in a permanent checking state.
The disc title is read during the emulator's normal physical-disc startup, so
no second mutating preflight session occurs.

The application does not request Android network access.

The Android runtime uses conservative CPU, frame-timing, and shader settings.
It identifies the Xbox title before renderer initialization and applies small
title-specific game profiles. Android defaults to 1× internal rendering and
the original 4:3 signal. The Morrowind profile enforces that baseline.
Morrowind, Halo, and Fable use a 30 Hz Android display request with host
vertical sync disabled, which preserves the Xbox's 60-field timing while
avoiding redundant panel refreshes. A profile can be disabled from Settings
when diagnosing a game.

The mobile adapter requests DVD 2×. QEMU coalesces adjacent small requests into
one forward-only 256 KiB memory window; it is never written to storage and is
destroyed when the session ends. There is no complete-disc or game-file cache,
and removing the drive ends emulation.

Android's public thermal-status API controls an optional display-brightness
limit while a game is running. **Manage heat** applies the limit only after the
device reports thermal pressure and restores the user's brightness when the
game closes.

The launcher, file pages, emulator picture, touch controls, pause menu, and FPS
overlay consume Android system-bar and display-cutout insets. Short landscape
screens use a compact launcher layout, while longer pages remain scrollable.

Renderer, scale, aspect, application-version, or profile changes invalidate
only disposable emulator shader, pipeline, and translation caches. Firmware,
the Xbox HDD, saves, and physical-disc data are never part of that graphics
cleanup.

## Build

Install JDK 21 and an Android SDK containing platform 36, Build Tools 36.1.0,
NDK 29.0.14206865, and CMake 3.30.3. Then run:

```sh
export JAVA_HOME=/path/to/jdk-21
export ANDROID_SDK_ROOT=/path/to/android-sdk
scripts/build_android.sh debug
```

The script checks out exact official xemu, SDL2, and libusb revisions outside
this source tree, applies the ordered Android patch series, and writes the APK
under `../gdox-output/release/android`.

Debug builds are suitable only for device testing. `build_android.sh release`
fails closed unless `GDOX_ANDROID_KEY_PROPERTIES` names a complete signing
configuration. The signing file stays outside the repository and generated
source tree. The script verifies the APK signature before publishing it.
Public APKs also require the complete corresponding-source bundle below.
Use an absolute `storeFile` path in that properties file.

After a clean committed build, create that bundle with:

```sh
make android-source
```

The resulting deterministic archive includes the exact patched emulator,
GDOX, libusb, native dependency, and GLib sources used by the APK.

## Device setup

Install the APK, open **Sources**, and choose:

- a 512-byte MCPX ROM;
- a compatible 1 MiB Xbox flash ROM;
- a writable raw or QCOW2 Xbox hard-disk image.

These files are copied into app-specific storage. They are emulator support
files, not game installations. GDOX does not include or distribute Microsoft
firmware. Attach the supported optical drive through a powered USB-C host
adapter, grant access, and insert an original Xbox disc.

Settings controls auto start, compatibility profiles, internal resolution,
4:3/16:9 presentation, image filtering, vertical sync, thermal management,
and disposable emulator-cache cleanup.

## Licensing

The Android emulator application is a derivative of xemu and must be
distributed under GPL-2.0 with complete corresponding source. GDOX's original
components remain available under the repository license, while the combined
APK follows GPL-2.0. Release publishing must include the exact patched xemu and
SDL2 trees, this GDOX tree, native dependency sources, build scripts, and
third-party notices.
