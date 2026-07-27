#include "test.h"

#include <stdio.h>
#include <string.h>

int gdox_test_failures = 0;

typedef struct test_group {
    const char *name;
    void (*run)(void);
} test_group;

static const test_group groups[] = {
    {"protocol", gdox_test_protocol},
    {"disc", gdox_test_disc},
    {"emulator", gdox_test_emulator},
    {"hash", gdox_test_hash},
    {"hdd_cache", gdox_test_hdd_cache},
    {"nbd", gdox_test_nbd},
    {"optical_monitor", gdox_test_optical_monitor},
    {"preferences", gdox_test_preferences},
    {"preservation_naming", gdox_test_preservation_naming},
    {"preserve", gdox_test_preserve},
    {"runtime_bundle", gdox_test_runtime_bundle},
    {"scsi_transport", gdox_test_scsi_transport},
    {"security", gdox_test_security},
    {"session", gdox_test_session},
    {"source", gdox_test_source},
    {"xdvdfs", gdox_test_xdvdfs},
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
