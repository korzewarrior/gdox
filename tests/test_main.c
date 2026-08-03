#include "test.h"

#include <stdio.h>
#include <string.h>

typedef struct test_group {
    const char *name;
    void (*run)(void);
} test_group;

static const test_group groups[] = {
    {"background_lifecycle", gdox_test_background_lifecycle},
    {"default_xbe_cache_source", gdox_test_default_xbe_cache_source},
    {"disc", gdox_test_disc},
    {"emulator", gdox_test_emulator},
    {"file_readahead_source", gdox_test_file_readahead_source},
    {"gamepad_input_policy", gdox_test_gamepad_input_policy},
    {"hash", gdox_test_hash},
    {"nbd", gdox_test_nbd},
    {"nbd_wire", gdox_test_nbd_wire},
    {"optical_monitor", gdox_test_optical_monitor},
    {"playback_labels", gdox_test_playback_labels},
    {"preferences", gdox_test_preferences},
    {"preservation_naming", gdox_test_preservation_naming},
    {"preserve", gdox_test_preserve},
    {"runtime_bundle", gdox_test_runtime_bundle},
    {"runtime_commands", gdox_test_runtime_commands},
    {"scsi_transport", gdox_test_scsi_transport},
    {"security", gdox_test_security},
    {"session_storage", gdox_test_session_storage},
    {"source", gdox_test_source},
    {"x360", gdox_test_x360},
    {"xbe_patch_source", gdox_test_xbe_patch_source},
    {"xdvdfs", gdox_test_xdvdfs},
    {"xdvdfs_directory_cache", gdox_test_xdvdfs_directory_cache},
    {"xemu_capabilities", gdox_test_xemu_capabilities},
    {"xemu_performance", gdox_test_xemu_performance},
    {"xemu_save_storage", gdox_test_xemu_save_storage},
    {"xenia_patches", gdox_test_xenia_patches},
    {"xenia_policy", gdox_test_xenia_policy},
    {"xenia_storage", gdox_test_xenia_storage},
};

static const test_group *find_group(const char *name)
{
    size_t index;

    for (index = 0U; index < sizeof(groups) / sizeof(groups[0]); ++index) {
        if (strcmp(groups[index].name, name) == 0) {
            return &groups[index];
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    size_t index;
    int argument;

    if (!gdox_test_initialize_program_path(argv[0])) {
        return 2;
    }
    if (argc < 2) {
        for (index = 0U; index < sizeof(groups) / sizeof(groups[0]); ++index) {
            groups[index].run();
        }
    } else {
        for (argument = 1; argument < argc; ++argument) {
            const test_group *group = find_group(argv[argument]);

            if (group == NULL) {
                (void)fprintf(
                    stderr,
                    "unknown test group: %s\n",
                    argv[argument]
                );
                return 2;
            }
            group->run();
        }
    }

    if (gdox_test_failures != 0) {
        (void)fprintf(stderr, "%d check(s) failed\n", gdox_test_failures);
        return 1;
    }
    (void)puts("GDOX tests passed");
    return 0;
}
