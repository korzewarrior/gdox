#ifndef GDOX_TEST_H
#define GDOX_TEST_H

#include <stdbool.h>
#include <stdio.h>

extern int gdox_test_failures;

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

void gdox_test_protocol(void);
void gdox_test_disc(void);
void gdox_test_emulator(void);
void gdox_test_hash(void);
void gdox_test_hdd_cache(void);
void gdox_test_nbd(void);
void gdox_test_optical_monitor(void);
void gdox_test_preferences(void);
void gdox_test_preservation_naming(void);
void gdox_test_preserve(void);
void gdox_test_runtime_bundle(void);
void gdox_test_security(void);
void gdox_test_session(void);
void gdox_test_source(void);
void gdox_test_xdvdfs(void);

#endif
