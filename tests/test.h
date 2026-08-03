#ifndef GDOX_TEST_H
#define GDOX_TEST_H

#include <stdbool.h>
#include <stdio.h>

extern int gdox_test_failures;
extern char gdox_test_program_path[4096];

bool gdox_test_initialize_program_path(const char *argument_zero);

#define GDOX_TEST_CHECK(expression)                                                            \
    do {                                                                                       \
        if (!(expression)) {                                                                   \
            (void)fprintf(                                                                     \
                stderr,                                                                        \
                "%s:%d: check failed: %s\n",                                                   \
                __FILE__,                                                                      \
                __LINE__,                                                                      \
                #expression                                                                   \
            );                                                                                 \
            ++gdox_test_failures;                                                              \
            return;                                                                            \
        }                                                                                      \
    } while (false)

void gdox_test_disc(void);
void gdox_test_background_lifecycle(void);
void gdox_test_default_xbe_cache_source(void);
void gdox_test_emulator(void);
void gdox_test_file_readahead_source(void);
void gdox_test_gamepad_input_policy(void);
void gdox_test_hash(void);
void gdox_test_nbd(void);
void gdox_test_nbd_wire(void);
void gdox_test_optical_monitor(void);
void gdox_test_playback_labels(void);
void gdox_test_preferences(void);
void gdox_test_preservation_naming(void);
void gdox_test_preserve(void);
void gdox_test_runtime_bundle(void);
void gdox_test_runtime_commands(void);
void gdox_test_scsi_transport(void);
void gdox_test_security(void);
void gdox_test_session_storage(void);
void gdox_test_source(void);
void gdox_test_x360(void);
void gdox_test_xbe_patch_source(void);
void gdox_test_xdvdfs(void);
void gdox_test_xdvdfs_directory_cache(void);
void gdox_test_xemu_capabilities(void);
void gdox_test_xemu_performance(void);
void gdox_test_xemu_save_storage(void);
void gdox_test_xenia_patches(void);
void gdox_test_xenia_policy(void);
void gdox_test_xenia_storage(void);

#endif
