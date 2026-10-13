#define _POSIX_C_SOURCE 200809L
#include "app/dolphin_profile.h"
#include "platform/user_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); ++failures; } } while (0)
int main(void)
{
    const char *temporary = getenv("TMPDIR");
    char root[GDOX_STORAGE_PATH_CAPACITY / 2U], ini[GDOX_STORAGE_PATH_CAPACITY];
    char gfx[GDOX_STORAGE_PATH_CAPACITY], config[GDOX_STORAGE_PATH_CAPACITY];
    gdox_error error; uint8_t *bytes = NULL; size_t count = 0U; bool found;
    const int written = snprintf(root, sizeof(root), "%s/gdox-profile-XXXXXX", temporary != NULL ? temporary : "/tmp");
    if (written < 0 || (size_t)written >= sizeof(root)) return 1;
    if (mkdtemp(root) == NULL) return 1;
    (void)snprintf(ini, sizeof(ini), "%s/Config/Dolphin.ini", root);
    (void)snprintf(gfx, sizeof(gfx), "%s/Config/GFX.ini", root);
    (void)snprintf(config, sizeof(config), "%s/Config", root);
    CHECK(gdox_dolphin_profile_prepare(root, &error));
    CHECK(gdox_storage_read(ini, 4096U, &bytes, &count, &found, &error) && found);
    CHECK(count != 0U); free(bytes); bytes = NULL;
    static const uint8_t custom[] = "[Core]\nGFXBackend = OGL\n[User]\nCustom = preserved\n";
    CHECK(gdox_storage_write_private(ini, custom, sizeof(custom) - 1U, true, &error));
    CHECK(gdox_dolphin_profile_prepare(root, &error));
    CHECK(gdox_storage_read(ini, 4096U, &bytes, &count, &found, &error));
    CHECK(count == sizeof(custom) - 1U && memcmp(bytes, custom, count) == 0);
    free(bytes);
    CHECK(unlink(ini) == 0);
    CHECK(symlink("GFX.ini", ini) == 0);
    CHECK(!gdox_dolphin_profile_prepare(root, &error));
    CHECK(unlink(ini) == 0 && unlink(gfx) == 0 && rmdir(config) == 0 && rmdir(root) == 0);
    return failures == 0 ? 0 : 1;
}
