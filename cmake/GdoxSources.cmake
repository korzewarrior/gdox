if(NOT DEFINED GDOX_ROOT)
    message(FATAL_ERROR "GDOX_ROOT must identify the repository root")
endif()

# Platform-independent live-disc services shared by desktop and Android.
set(
    GDOX_LIVE_DISC_SOURCES
    "${GDOX_ROOT}/src/core/default_xbe_cache_source.c"
    "${GDOX_ROOT}/src/core/disc.c"
    "${GDOX_ROOT}/src/core/error.c"
    "${GDOX_ROOT}/src/core/file_readahead_source.c"
    "${GDOX_ROOT}/src/core/live.c"
    "${GDOX_ROOT}/src/core/source.c"
    "${GDOX_ROOT}/src/core/xbe_patch_source.c"
    "${GDOX_ROOT}/src/core/xdvdfs.c"
    "${GDOX_ROOT}/src/core/xdvdfs_directory_cache.c"
)

# Compact-XISO construction is used only by preservation workflows.
set(
    GDOX_PRESERVATION_MEDIA_SOURCES
    "${GDOX_ROOT}/src/core/compact.c"
)

# Xbox 360 media parsing is desktop-only until another application target
# provides a complete Xenia runtime and distribution contract.
set(
    GDOX_XBOX360_MEDIA_SOURCES
    "${GDOX_ROOT}/src/core/x360.c"
)

# MT1887 optical services shared by desktop and Android hosts.
set(
    GDOX_MT1887_OPTICAL_SOURCES
    "${GDOX_ROOT}/src/platform/mmc_commands.c"
    "${GDOX_ROOT}/src/platform/mt1887_media_profile.c"
    "${GDOX_ROOT}/src/platform/mt1887_source.c"
    "${GDOX_ROOT}/src/platform/mt1887_profile.c"
    "${GDOX_ROOT}/src/platform/scsi_transport.c"
    "${GDOX_ROOT}/src/platform/usb_bot_identity.c"
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
