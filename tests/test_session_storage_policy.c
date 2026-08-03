#include "platform/session_storage.h"
#include "platform/session_storage_policy.h"

#include <stdio.h>
#include <string.h>

typedef struct path_vector {
    const char *relative;
    bool accepted;
} path_vector;

typedef struct recovery_vector {
    bool trusted_owner;
    gdox_session_lock_observation lock;
    gdox_session_recovery_state expected;
} recovery_vector;

static const path_vector path_vectors[] = {
#define GDOX_SESSION_PATH_VECTOR(relative, accepted) {relative, accepted},
#define GDOX_SESSION_RECOVERY_VECTOR(trusted_owner, lock, expected)
#include "session_storage_policy_vectors.def"
#undef GDOX_SESSION_RECOVERY_VECTOR
#undef GDOX_SESSION_PATH_VECTOR
};

static const recovery_vector recovery_vectors[] = {
#define GDOX_SESSION_PATH_VECTOR(relative, accepted)
#define GDOX_SESSION_RECOVERY_VECTOR(trusted_owner, lock, expected) \
    {trusted_owner, lock, expected},
#include "session_storage_policy_vectors.def"
#undef GDOX_SESSION_RECOVERY_VECTOR
#undef GDOX_SESSION_PATH_VECTOR
};

static int failures;

static void check(bool condition, const char *message)
{
    if (!condition) {
        (void)fprintf(stderr, "session-storage policy: %s\n", message);
        ++failures;
    }
}

static void test_relative_paths(void)
{
    gdox_session_storage storage = {0};
    char output[GDOX_SESSION_PATH_CAPACITY];
    gdox_error error;
    size_t index;

#if defined(_WIN32)
    (void)snprintf(storage.root, sizeof(storage.root), "C:/gdox-session-test");
#else
    (void)snprintf(storage.root, sizeof(storage.root), "/gdox-session-test");
#endif
    storage.active = true;
    for (index = 0U;
         index < sizeof(path_vectors) / sizeof(path_vectors[0]);
         ++index) {
        check(
            gdox_session_relative_path_is_safe(path_vectors[index].relative)
                == path_vectors[index].accepted,
            "relative path decision differs from the shared vector"
        );
        check(
            gdox_session_storage_path(
                &storage,
                path_vectors[index].relative,
                output,
                &error
            ) == path_vectors[index].accepted,
            "platform path decision differs from the shared vector"
        );
    }
}

static void test_owner_marker(void)
{
    static const char expected[] =
        "GDOX-SESSION-OWNER-V1\nname=session-42-token\n";
    char marker[GDOX_SESSION_MARKER_CAPACITY];
    char long_name[GDOX_SESSION_MARKER_CAPACITY];
    size_t bytes = 0U;

    check(gdox_session_owner_marker_format(
        "session-42-token", marker, &bytes
    ), "owner marker formats");
    check(bytes == sizeof(expected) - 1U, "owner marker length is exact");
    check(memcmp(marker, expected, sizeof(expected) - 1U) == 0,
        "owner marker bytes are stable");
    check(!gdox_session_owner_marker_format(NULL, marker, &bytes),
        "null owner name is rejected");
    check(!gdox_session_owner_marker_format("session\nforged", marker, &bytes),
        "multiline owner name is rejected");
    memset(long_name, 'x', sizeof(long_name));
    long_name[sizeof(long_name) - 1U] = '\0';
    check(!gdox_session_owner_marker_format(long_name, marker, &bytes),
        "oversized owner marker is rejected");
}

static void test_recovery_transitions(void)
{
    size_t index;

    for (index = 0U;
         index < sizeof(recovery_vectors) / sizeof(recovery_vectors[0]);
         ++index) {
        check(
            gdox_session_recovery_decide(
                recovery_vectors[index].trusted_owner,
                recovery_vectors[index].lock
            ) == recovery_vectors[index].expected,
            "recovery decision differs from the shared vector"
        );
    }
}

int main(void)
{
    test_relative_paths();
    test_owner_marker();
    test_recovery_transitions();
    return failures == 0 ? 0 : 1;
}
