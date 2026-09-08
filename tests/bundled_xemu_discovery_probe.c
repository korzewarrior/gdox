#include "gdox/emulator.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char path[GDOX_EMULATOR_PATH_CAPACITY];
    gdox_error error;
    const bool automatic = argc == 2
        && strcmp(argv[1], "--automatic") == 0;
    const bool found = automatic
        ? gdox_emulator_discover_executable(path, &error)
        : gdox_emulator_discover_bundled_executable(path, &error);

    if (!found) {
        (void)fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    (void)printf("%s\n", path);
    return 0;
}
