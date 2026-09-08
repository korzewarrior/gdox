#include "gdox/emulator.h"
#include "app/runtime_bundle.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char path[GDOX_EMULATOR_PATH_CAPACITY];
    gdox_error error;
    if (argc == 2 && strcmp(argv[1], "--prepare") == 0) {
        gdox_runtime_bundle_status bundle;
        if (!gdox_runtime_bundle_prepare(
                GDOX_XEMU_INCLUDED_SELECTION, &bundle, &error)
            || !bundle.bundled || !bundle.xemu_available
            || !bundle.configuration_ready || !bundle.hdd_ready
            || !bundle.persistent_save_export || !bundle.full_hdd_isolation) {
            (void)fprintf(stderr, "included runtime setup failed: %s\n", error.message);
            return 1;
        }
        (void)printf("included_runtime_ready\nxemu=%s\nhdd=%s\nconfiguration=%s\n",
            bundle.executable, bundle.hdd, bundle.configuration);
        return 0;
    }
    const bool automatic = argc == 2
        && strcmp(argv[1], "--automatic") == 0;
    const bool found = automatic
        ? gdox_emulator_discover_executable(path, &error)
        : gdox_emulator_discover_bundled_executable(path, &error);

    if (!found) {
        (void)fprintf(stderr, "%s\n", error.message);
        if (path[0] != '\0') {
            (void)fprintf(stderr, "expected=%s\n", path);
        }
        return 1;
    }
    (void)printf("%s\n", path);
    return 0;
}
