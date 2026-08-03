#include "test_xemu_helper.h"

#include <string.h>

int gdox_test_xemu_gameplay(int argc, char **argv)
{
    int argument = 1;

    if (argc < 8
        || strcmp(argv[argument++], "--gdox-runtime") != 0
        || strcmp(argv[argument++], "--gdox-save-vault") != 0
        || argv[argument][0] == '\0') {
        return 2;
    }
    ++argument;
    if (strcmp(argv[argument++], "-config_path") != 0
        || argv[argument][0] == '\0') {
        return 2;
    }
    ++argument;
    if (argument < argc && strcmp(argv[argument], "-full-screen") == 0) {
        ++argument;
    }
    if (argument + 2 != argc
        || strcmp(argv[argument++], "-dvd_path") != 0
        || argv[argument][0] == '\0') {
        return 2;
    }
    return 0;
}
