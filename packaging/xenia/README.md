# Patched Xenia Windows runtime

GDOX uses two exact Xenia Canary commits. The Windows recipe applies the
patches listed in `packaging/runtime-manifest.json` to add the local,
read-only loopback NBD disc transport and bind it to the export length validated
by GDOX. The same reviewed Windows executables run through Proton on Linux and
Steam Deck, using the image-path form of the local read-only bridge.

`patches/0004-gdox-ephemeral-game-content.patch` is part of both reviewed
runtimes.
It routes installed titles, downloadable content, title updates, guest cache
packages, gamer pictures, and other non-save content into the session storage
root when `--gdox_persistent_content_saves_only=true` is used. Saved games,
Xbox saved games, and profiles continue to use the persistent content root. It
also moves Xenia's title-relaunch data into the session storage root and adds
an exact, side-effect-free `--gdox-storage-capabilities` query.

`patches/0005-gdox-managed-disclaimer.patch` adds the transient
`--gdox_disclaimer_acknowledged=true` launch contract. Managed GDOX sessions
use it to acknowledge Xenia's usage notice without showing the Quickstart
prompt, opening a browser, or storing the acknowledgement in the disposable
Wine prefix. The capability query reports this contract so pre-patch
executables cannot be admitted by manifest metadata alone.

Both artifacts recorded in the manifest contain patches 0004 and 0005, answer
the exact capability query, and are pinned by archive and executable size and
SHA-256. The application generator and release packager also require
separately reviewed executable digests; manifest options alone cannot enable
storage isolation or managed disclaimer acknowledgement.

The manifest pins the source commits, licenses, patches, toolchain, Vulkan SDK
installer, build recipe, packager, executables, and archives by size and
SHA-256. The recipe also pins Git line endings, `SOURCE_DATE_EPOCH`, the Xenia
version header, compiler path mappings, deterministic compiler and linker
flags, and the CodeView PDB basename. A full PDB remains only in the temporary
work root; the runtime archive contains only `LICENSE` and
`xenia_canary.exe`.

## Build environment

Use an x64 Visual Studio 2022 Build Tools prompt. The exact versions are in the
runtime manifest. The reviewed environment uses `C:\BuildTools` for Visual
Studio Build Tools and `C:\dvds-tools` for Git, Python, CMake, and Ninja. Those
roots are part of the deterministic path mappings; a different build result is
rejected by the pinned executable digest.

Download the exact Vulkan SDK installer named in the manifest. The recipe
verifies its size and digest, then installs an isolated `copy_only` SDK inside
the work root. Do not set `VULKAN_SDK` manually.

The verified installer file may be kept and supplied to both revision builds;
it is the reusable acquisition artifact. Each recipe invocation deliberately
uses empty, independent work and output trees and performs a fresh isolated SDK
install, checkout, `xb setup`, configure, build, PDB creation, and packaging.
The expanded SDK and Xenia setup trees are not caches: `xb setup` obtains and
mutates build inputs that are not independently pinned as a complete tree, so
reusing them would weaken the build provenance check.

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  packaging\xenia\build-windows.ps1 `
  -Revision 72ce13097 `
  -WorkRoot C:\gdox-build\xenia-72ce13097 `
  -OutputDirectory C:\gdox-build\output-72ce13097 `
  -VulkanInstaller C:\gdox-tools\vulkansdk-windows-X64-1.4.357.0.exe
```

Repeat with revision `7d8be7f17`, a new empty work root, and a new empty output
directory. The recipe refuses non-empty directories and prints the verified
source, executable, and archive metadata when it succeeds.

The patched Windows assets are distributed from the exact durable HTTPS URLs
recorded in the manifest. Desktop package creation rejects an unpublished
candidate or a different archive. Do not substitute an upstream release URL;
an upstream archive does not contain the reviewed patched executable.

For a future runtime revision that has not been published, stage the complete
reviewed candidate from the exact archives. First place the two verified
archives, and no other files, in one new directory:

```powershell
New-Item -ItemType Directory C:\gdox-build\candidates
Copy-Item C:\gdox-build\output-72ce13097\*.zip C:\gdox-build\candidates
Copy-Item C:\gdox-build\output-7d8be7f17\*.zip C:\gdox-build\candidates
```

Then stage the candidate runtime with:

```powershell
python scripts\private_candidate_runtime.py bundle `
  --target x86_64-pc-windows-msvc `
  --candidate-directory C:\gdox-build\candidates `
  --destination C:\gdox-build\private-runtime
```

This command verifies each candidate archive and writes
`VERSIONS.json` and `SOURCE.md` metadata.

To create a complete Windows validation package, provide the reviewed GDOX
artifact and reviewed patched xemu executable to the candidate packager:

```powershell
python scripts\package_private_candidate.py `
  --target x86_64-pc-windows-msvc `
  --artifact C:\gdox-build\gdox.exe `
  --candidate-runtime-directory C:\gdox-build\candidates `
  --candidate-xemu-executable C:\gdox-build\xemu.exe
```

Only the candidate-validation entrypoints accept candidate assets or produce
an archive whose name ends in `-candidate`. The public runtime fetcher and
release packager expose no candidate mode.
