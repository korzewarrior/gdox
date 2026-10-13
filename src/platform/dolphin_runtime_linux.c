#define _POSIX_C_SOURCE 200809L
#include "gdox/dolphin.h"
#include "gdox/hash.h"
#include "platform/disc_bridge_linux.h"
#include "platform/dolphin_payload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool candidate(const char *root, gdox_dolphin_runtime *runtime, gdox_error *error)
{
    gdox_hashes hashes;
    char digest[GDOX_SHA256_BYTES * 2U + 1U];
    uint64_t length;
    int a, b;
    if (root == NULL || root[0] != '/') return false;
    a = snprintf(runtime->launcher, sizeof(runtime->launcher), "%s/dolphin/dolphin", root);
    b = snprintf(runtime->payload, sizeof(runtime->payload), "%s/dolphin/bin/dolphin-emu", root);
    if (a < 0 || b < 0 || (size_t)a >= sizeof(runtime->launcher)
        || (size_t)b >= sizeof(runtime->payload)
        || access(runtime->launcher, X_OK) != 0 || access(runtime->payload, X_OK) != 0) return false;
    if (!gdox_hash_file(runtime->payload, &hashes, &length, error)) return false;
    gdox_hash_hex(hashes.sha256, sizeof(hashes.sha256), false, digest);
    if (length != GDOX_DOLPHIN_PAYLOAD_BYTES || strcmp(digest, GDOX_DOLPHIN_PAYLOAD_SHA256) != 0) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "Bundled Dolphin does not match the reviewed release");
        return false;
    }
    if (!gdox_hash_file(runtime->launcher, &hashes, &length, error)) return false;
    gdox_hash_hex(hashes.sha256, sizeof(hashes.sha256), false, digest);
    if (length != GDOX_DOLPHIN_LAUNCHER_BYTES || strcmp(digest, GDOX_DOLPHIN_LAUNCHER_SHA256) != 0) {
        gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "Bundled Dolphin launcher does not match the reviewed release");
        return false;
    }
    return gdox_disc_bridge_preflight(error);
}

bool gdox_dolphin_resolve(gdox_dolphin_runtime *runtime, gdox_error *error)
{
    char executable[GDOX_EMULATOR_PATH_CAPACITY];
    char root[GDOX_EMULATOR_PATH_CAPACITY];
    const char *override = getenv("GDOX_RUNTIME_DIR");
    ssize_t bytes;
    char *slash;
    gdox_error_clear(error);
    if (runtime == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "Dolphin runtime output is required");
        return false;
    }
    memset(runtime, 0, sizeof(*runtime));
    if (override != NULL && override[0] != '\0') {
        if (candidate(override, runtime, error)) return true;
        goto unavailable;
    }
    bytes = readlink("/proc/self/exe", executable, sizeof(executable) - 1U);
    if (bytes <= 0 || (size_t)bytes >= sizeof(executable) - 1U) goto unavailable;
    executable[bytes] = '\0';
    slash = strrchr(executable, '/');
    if (slash == NULL) goto unavailable;
    *slash = '\0';
    const char *suffixes[] = {"runtime", "../runtime"};
    for (size_t i = 0U; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        const int written = snprintf(root, sizeof(root), "%s/%s", executable, suffixes[i]);
        if (written >= 0 && (size_t)written < sizeof(root) && candidate(root, runtime, error)) return true;
        if (gdox_error_is_set(error)) break;
    }
unavailable:
    memset(runtime, 0, sizeof(*runtime));
    if (!gdox_error_is_set(error)) {
        gdox_error_set(error, GDOX_ERROR_NOT_FOUND, "Verified bundled Dolphin is unavailable; reinstall the complete GDOX package");
    }
    return false;
}
