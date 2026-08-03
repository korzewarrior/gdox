#include "app/xenia_patches.h"

#include "platform/user_storage.h"

#include <stdio.h>

#if defined(_WIN32)
#define GDOX_XENIA_PATCH_RELATIVE \
    "patches\\4D5307E8 - Mass Effect.patch.toml"
#define GDOX_XENIA_PATH_SEPARATOR "\\"
#else
#define GDOX_XENIA_PATCH_RELATIVE \
    "patches/4D5307E8 - Mass Effect.patch.toml"
#define GDOX_XENIA_PATH_SEPARATOR "/"
#endif

/*
 * Rendering fixes from xenia-canary/game-patches commit
 * 12c7e7e05b9c6d4daffa6ed92826b6f7d6e3ebe6. Author fields are retained in
 * the patch data and application remains gated by Xenia's module hash check.
 */
static const uint8_t mass_effect_world_rendering_patch[] =
    "title_name = \"Mass Effect\"\n"
    "title_id = \"4D5307E8\"\n"
    "hash = \"D29E208F252C9051\"\n"
    "\n"
    "[[patch]]\n"
    "    name = \"Black Shading Fix\"\n"
    "    desc = \"Disables MSAA.\"\n"
    "    author = \"HouseofTudor\"\n"
    "    is_enabled = true\n"
    "\n"
    "    [[patch.be8]]\n"
    "        address = 0x82e5dcd7\n"
    "        value = 0x00\n"
    "\n"
    "[[patch]]\n"
    "    name = \"Flickering Decals Fix\"\n"
    "    desc = \"Fixes decals (bloodpools, bullet holes, etc.) that "
        "flicker when using the Vulkan FBO or D3D12 RTV render paths.\"\n"
    "    author = \"boma\"\n"
    "    is_enabled = true\n"
    "\n"
    "    [[patch.be32]]\n"
    "        address = 0x8263ca88\n"
    "        value = 0x4bf994dc\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5f68\n"
    "        value = 0x9421fff0\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5f6c\n"
    "        value = 0x3c00bc23\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5f70\n"
    "        value = 0x6000d70a\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5f74\n"
    "        value = 0x9001000c\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5f78\n"
    "        value = 0xc001000c\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5f7c\n"
    "        value = 0x38210010\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5f80\n"
    "        value = 0x48066b0c\n"
    "    [[patch.be32]]\n"
    "        address = 0x8263cf28\n"
    "        value = 0x4bf99094\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5fbc\n"
    "        value = 0x3d80c000\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5fc0\n"
    "        value = 0x618c0000\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5fc4\n"
    "        value = 0x918101b4\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5fc8\n"
    "        value = 0xc00101b4\n"
    "    [[patch.be32]]\n"
    "        address = 0x825d5fcc\n"
    "        value = 0x48066f60\n";

static bool build_patch_path(
    const char *storage_root,
    char output[GDOX_STORAGE_PATH_CAPACITY],
    gdox_error *error
)
{
    static const char relative[] = GDOX_XENIA_PATCH_RELATIVE;
    size_t root_bytes;
    const char *separator;
    int written;

    if (storage_root == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia storage root is required"
        );
        return false;
    }
    for (root_bytes = 0U;
         root_bytes < GDOX_STORAGE_PATH_CAPACITY
            && storage_root[root_bytes] != '\0';
         ++root_bytes) {
    }
    if (root_bytes == 0U
        || root_bytes == GDOX_STORAGE_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            root_bytes == 0U
                ? "Xenia storage root is required"
                : "Xenia storage root is too long"
        );
        return false;
    }
    separator = storage_root[root_bytes - 1U] == '/'
            || storage_root[root_bytes - 1U] == '\\'
        ? ""
        : GDOX_XENIA_PATH_SEPARATOR;
    written = snprintf(
        output,
        GDOX_STORAGE_PATH_CAPACITY,
        "%s%s%s",
        storage_root,
        separator,
        relative
    );
    if (written < 0 || (size_t)written >= GDOX_STORAGE_PATH_CAPACITY) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "Xenia patch path is too long"
        );
        return false;
    }
    return true;
}

bool gdox_xenia_provision_patches(
    const char *storage_root,
    gdox_xenia_patch_set patch_set,
    gdox_error *error
)
{
    char path[GDOX_STORAGE_PATH_CAPACITY];

    gdox_error_clear(error);
    if (!build_patch_path(storage_root, path, error)) {
        return false;
    }
    switch (patch_set) {
        case GDOX_XENIA_PATCH_SET_NONE:
            return true;
        case GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1:
            return gdox_storage_write_private(
                path,
                mass_effect_world_rendering_patch,
                sizeof(mass_effect_world_rendering_patch) - 1U,
                true,
                error
            );
    }
    gdox_error_set(
        error,
        GDOX_ERROR_INVALID_ARGUMENT,
        "Xenia patch set is not supported"
    );
    return false;
}
