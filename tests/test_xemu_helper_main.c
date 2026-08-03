#include "test_xemu_helper.h"

#include "core/xemu_capabilities.h"

#include <string.h>

int main(int argc, char **argv)
{
    if (argc == 2
        && strcmp(argv[1], GDOX_XEMU_CAPABILITIES_ARGUMENT) == 0) {
        return gdox_test_xemu_capabilities();
    }
    if (argc > 1
        && (strcmp(argv[1], "--gdox-migrate-hdd") == 0
            || strcmp(argv[1], "--gdox-validate-save-vault") == 0)) {
        return gdox_test_xemu_save(argc, argv);
    }
    if (argc > 1 && strcmp(argv[1], "--gdox-runtime") == 0) {
        return gdox_test_xemu_gameplay(argc, argv);
    }
    return 2;
}
