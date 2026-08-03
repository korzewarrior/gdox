#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include "app/xenia_patches.h"
#include "platform/user_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#define gdox_test_getpid _getpid
#define gdox_test_remove _unlink
#define gdox_test_rmdir _rmdir
#define GDOX_TEST_PATH_SEPARATOR "\\"
#else
#include <unistd.h>
#define gdox_test_getpid getpid
#define gdox_test_remove unlink
#define gdox_test_rmdir rmdir
#define GDOX_TEST_PATH_SEPARATOR "/"
#endif

static const uint8_t expected_mass_effect_patch[] =
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

static bool read_exact_patch(const char *path)
{
    uint8_t *data = NULL;
    size_t bytes = 0U;
    bool found = false;
    gdox_error error;
    bool matches;

    if (!gdox_storage_read(
            path,
            sizeof(expected_mass_effect_patch),
            &data,
            &bytes,
            &found,
            &error
        )) {
        return false;
    }
    matches = found
        && bytes == sizeof(expected_mass_effect_patch) - 1U
        && memcmp(
            data,
            expected_mass_effect_patch,
            sizeof(expected_mass_effect_patch) - 1U
        ) == 0;
    free(data);
    return matches;
}

static size_t count_text(const char *text, const char *needle)
{
    size_t count = 0U;
    const size_t needle_bytes = strlen(needle);

    while ((text = strstr(text, needle)) != NULL) {
        ++count;
        text += needle_bytes;
    }
    return count;
}

void gdox_test_xenia_patches(void)
{
    char root[256];
    char patches[320];
    char patch_path[384];
    char blocked_root[256];
    char long_root[GDOX_STORAGE_PATH_CAPACITY];
    const long long stamp = (long long)time(NULL);
    gdox_error error;
    uint64_t ignored_size;

    (void)snprintf(
        root,
        sizeof(root),
        "./gdox-xenia-patches-%d-%lld",
        gdox_test_getpid(),
        stamp
    );
    (void)snprintf(
        patches,
        sizeof(patches),
        "%s%spatches",
        root,
        GDOX_TEST_PATH_SEPARATOR
    );
    (void)snprintf(
        patch_path,
        sizeof(patch_path),
        "%s%s4D5307E8 - Mass Effect.patch.toml",
        patches,
        GDOX_TEST_PATH_SEPARATOR
    );
    (void)snprintf(
        blocked_root,
        sizeof(blocked_root),
        "./gdox-xenia-patches-blocked-%d-%lld",
        gdox_test_getpid(),
        stamp
    );
    (void)gdox_test_remove(patch_path);
    (void)gdox_test_rmdir(patches);
    (void)gdox_test_rmdir(root);
    (void)gdox_test_remove(blocked_root);

    GDOX_TEST_CHECK(gdox_storage_ensure_directory(root, &error));
    GDOX_TEST_CHECK(gdox_xenia_provision_patches(
        root, GDOX_XENIA_PATCH_SET_NONE, &error
    ));
    GDOX_TEST_CHECK(!gdox_storage_file_size(patch_path, &ignored_size));

    GDOX_TEST_CHECK(gdox_xenia_provision_patches(
        root,
        GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1,
        &error
    ));
    GDOX_TEST_CHECK(read_exact_patch(patch_path));
    GDOX_TEST_CHECK(count_text(
        (const char *)expected_mass_effect_patch, "[[patch.be32]]"
    ) == 14U);
    GDOX_TEST_CHECK(count_text(
        (const char *)expected_mass_effect_patch, "[[patch]]"
    ) == 2U);
    GDOX_TEST_CHECK(count_text(
        (const char *)expected_mass_effect_patch, "is_enabled = true"
    ) == 2U);
    GDOX_TEST_CHECK(strstr(
        (const char *)expected_mass_effect_patch, "60 FPS"
    ) == NULL);
    GDOX_TEST_CHECK(strstr(
        (const char *)expected_mass_effect_patch, "Upscaling"
    ) == NULL);
    GDOX_TEST_CHECK(strstr(
        (const char *)expected_mass_effect_patch, "Skip Intro"
    ) == NULL);
    GDOX_TEST_CHECK(strstr(
        (const char *)expected_mass_effect_patch, "Anisotropic"
    ) == NULL);
    GDOX_TEST_CHECK(strstr(
        (const char *)expected_mass_effect_patch, "hash check"
    ) == NULL);
    GDOX_TEST_CHECK(strstr(
        (const char *)expected_mass_effect_patch, "Frametime"
    ) == NULL);

    GDOX_TEST_CHECK(gdox_storage_write_private(
        patch_path, (const uint8_t *)"stale", 5U, true, &error
    ));
    GDOX_TEST_CHECK(gdox_xenia_provision_patches(
        root,
        GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1,
        &error
    ));
    GDOX_TEST_CHECK(read_exact_patch(patch_path));
    GDOX_TEST_CHECK(gdox_xenia_provision_patches(
        root,
        GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1,
        &error
    ));
    GDOX_TEST_CHECK(read_exact_patch(patch_path));

    GDOX_TEST_CHECK(!gdox_xenia_provision_patches(
        NULL, GDOX_XENIA_PATCH_SET_NONE, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    GDOX_TEST_CHECK(!gdox_xenia_provision_patches(
        root, (gdox_xenia_patch_set)99, &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);
    memset(long_root, 'x', sizeof(long_root) - 1U);
    long_root[sizeof(long_root) - 1U] = '\0';
    GDOX_TEST_CHECK(!gdox_xenia_provision_patches(
        long_root,
        GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1,
        &error
    ));
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_INVALID_ARGUMENT);

    GDOX_TEST_CHECK(gdox_storage_write_private(
        blocked_root, (const uint8_t *)"file", 4U, true, &error
    ));
    GDOX_TEST_CHECK(!gdox_xenia_provision_patches(
        blocked_root,
        GDOX_XENIA_PATCH_SET_MASS_EFFECT_WORLD_RENDERING_V1,
        &error
    ));
    GDOX_TEST_CHECK(gdox_error_is_set(&error));

    GDOX_TEST_CHECK(gdox_test_remove(patch_path) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(patches) == 0);
    GDOX_TEST_CHECK(gdox_test_rmdir(root) == 0);
    GDOX_TEST_CHECK(gdox_test_remove(blocked_root) == 0);
}
