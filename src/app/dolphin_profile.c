#include "app/dolphin_profile.h"
#include "platform/user_storage.h"
#include <stdio.h>
#include <string.h>

bool gdox_dolphin_profile_prepare(const char *directory, gdox_error *error)
{
    static const char core[] = "[Core]\nGFXBackend = Vulkan\nLoadGameIntoMemory = False\n";
    static const char graphics[] = "[Settings]\nShaderCompilationMode = 2\n";
    char config[GDOX_STORAGE_PATH_CAPACITY];
    char ini[GDOX_STORAGE_PATH_CAPACITY];
    char gfx[GDOX_STORAGE_PATH_CAPACITY];
    bool found = false;
    gdox_error_clear(error);
    if (directory == NULL || directory[0] == '\0'
        || strlen(directory) + sizeof("/Config/Dolphin.ini") > sizeof(ini)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "A managed Dolphin profile directory is required");
        return false;
    }
    (void)snprintf(config, sizeof(config), "%s/Config", directory);
    (void)snprintf(ini, sizeof(ini), "%s/Config/Dolphin.ini", directory);
    (void)snprintf(gfx, sizeof(gfx), "%s/Config/GFX.ini", directory);
    if (!gdox_storage_ordinary_file(ini, &found, error)) return false;
    if (found) return true;
    if (!gdox_storage_ensure_private_directory(config, error)
        || !gdox_storage_ordinary_file(gfx, &found, error)) return false;
    if (!found && !gdox_storage_write_private(gfx, (const uint8_t *)graphics,
            sizeof(graphics) - 1U, false, error)) return false;
    return gdox_storage_write_private(ini, (const uint8_t *)core,
        sizeof(core) - 1U, false, error);
}
