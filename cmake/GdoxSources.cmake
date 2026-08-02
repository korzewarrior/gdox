if(NOT DEFINED GDOX_ROOT)
    message(FATAL_ERROR "GDOX_ROOT must identify the repository root")
endif()

# Platform-independent live-disc services shared by desktop and Android.
set(
    GDOX_LIVE_DISC_SOURCES
    "${GDOX_ROOT}/src/core/compact.c"
    "${GDOX_ROOT}/src/core/disc.c"
    "${GDOX_ROOT}/src/core/error.c"
    "${GDOX_ROOT}/src/core/live.c"
    "${GDOX_ROOT}/src/core/source.c"
    "${GDOX_ROOT}/src/core/xdvdfs.c"
)

# Platform-neutral Xbox HDD scratch-partition maintenance.
set(
    GDOX_HDD_CACHE_SOURCES
    "${GDOX_ROOT}/src/core/hdd_cache.c"
)

# Core services used by the desktop preservation and emulator workflow.
set(
    GDOX_DESKTOP_CORE_SOURCES
    "${GDOX_ROOT}/src/core/hash.c"
    "${GDOX_ROOT}/src/core/media_image.c"
    "${GDOX_ROOT}/src/core/preserve.c"
    "${GDOX_ROOT}/src/core/preservation_catalog.c"
    "${GDOX_ROOT}/src/core/preservation_manifest.c"
    "${GDOX_ROOT}/src/core/security.c"
)
