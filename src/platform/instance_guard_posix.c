#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/instance_guard.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#if !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif

static const char lock_suffix[] = "/gdox-desktop.lock";
static const char activation_suffix[] = "/gdox-desktop.activate";

struct gdox_instance_guard {
    int descriptor;
    int activation_socket;
    char activation_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
};

static bool trusted_runtime_directory(const char *path)
{
    struct stat status;

    return path != NULL && path[0] != '\0'
        && lstat(path, &status) == 0
        && S_ISDIR(status.st_mode)
        && status.st_uid == getuid()
        && (status.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

static bool directory_paths_fit(const char *path)
{
    size_t path_bytes;

    if (path == NULL) {
        return false;
    }
    path_bytes = strlen(path);
    return path_bytes + sizeof(activation_suffix)
            <= sizeof(((struct sockaddr_un *)0)->sun_path)
        && path_bytes + sizeof(lock_suffix) <= 512U;
}

static bool select_runtime_directory(char *output, size_t capacity)
{
    const char *candidates[] = {
        getenv("XDG_RUNTIME_DIR"),
        getenv("TMPDIR"),
    };
    size_t index;
    int bytes;

    for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (trusted_runtime_directory(candidates[index])
            && directory_paths_fit(candidates[index])) {
            bytes = snprintf(output, capacity, "%s", candidates[index]);
            return bytes >= 0 && (size_t)bytes < capacity;
        }
    }
    bytes = snprintf(
        output,
        capacity,
        "/tmp/gdox-runtime-%lu",
        (unsigned long)getuid()
    );
    if (bytes < 0 || (size_t)bytes >= capacity
        || !directory_paths_fit(output)) {
        return false;
    }
    if (mkdir(output, 0700) != 0 && errno != EEXIST) {
        return false;
    }
    return trusted_runtime_directory(output);
}

static bool build_path(
    char *output,
    size_t capacity,
    const char *runtime_directory,
    const char *suffix
)
{
    const int bytes = snprintf(
        output, capacity, "%s%s", runtime_directory, suffix
    );

    return bytes >= 0 && (size_t)bytes < capacity;
}

static bool configure_activation_socket(int descriptor)
{
    int descriptor_flags = fcntl(descriptor, F_GETFD);
    int status_flags = fcntl(descriptor, F_GETFL);

    return descriptor_flags >= 0
        && status_flags >= 0
        && fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) == 0
        && fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) == 0;
}

static bool activation_address(
    const char *path,
    struct sockaddr_un *address,
    socklen_t *address_bytes
)
{
    size_t path_bytes;

    if (path == NULL || address == NULL || address_bytes == NULL) {
        return false;
    }
    path_bytes = strlen(path);
    if (path_bytes + 1U > sizeof(address->sun_path)) {
        return false;
    }
    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, path, path_bytes + 1U);
    *address_bytes = (socklen_t)(
        offsetof(struct sockaddr_un, sun_path) + path_bytes + 1U
    );
    return true;
}

static int create_activation_listener(const char *path)
{
    struct sockaddr_un address;
    socklen_t address_bytes;
    int descriptor;

    if (!activation_address(path, &address, &address_bytes)) {
        return -1;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        return -1;
    }
    descriptor = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (descriptor < 0) {
        return -1;
    }
    if (!configure_activation_socket(descriptor)
        || bind(
            descriptor,
            (const struct sockaddr *)&address,
            address_bytes
        ) != 0
        || chmod(path, 0600) != 0) {
        (void)close(descriptor);
        (void)unlink(path);
        return -1;
    }
    return descriptor;
}

static bool trusted_activation_endpoint(const char *path)
{
    struct stat status;

    return lstat(path, &status) == 0
        && S_ISSOCK(status.st_mode)
        && status.st_uid == getuid()
        && (status.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

static void activation_retry_delay(void)
{
    const struct timespec duration = {0, 10000000L};

    (void)nanosleep(&duration, NULL);
}

gdox_instance_guard *gdox_instance_guard_acquire(bool *already_running)
{
    char runtime_directory[384];
    char lock_path[512];
    char activation_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    gdox_instance_guard *guard;
    struct stat status;
    int activation_socket;
    int descriptor;

    if (already_running != NULL) {
        *already_running = false;
    }
    if (!select_runtime_directory(
            runtime_directory, sizeof(runtime_directory)
        )
        || !build_path(
            lock_path,
            sizeof(lock_path),
            runtime_directory,
            lock_suffix
        )
        || !build_path(
            activation_path,
            sizeof(activation_path),
            runtime_directory,
            activation_suffix
        )) {
        return NULL;
    }
    descriptor = open(
        lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600
    );
    if (descriptor < 0) {
        return NULL;
    }
    if (fstat(descriptor, &status) != 0
        || !S_ISREG(status.st_mode)
        || status.st_uid != getuid()
        || (status.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        (void)close(descriptor);
        return NULL;
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        if (already_running != NULL
            && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            *already_running = true;
        }
        (void)close(descriptor);
        return NULL;
    }
    activation_socket = create_activation_listener(activation_path);
    if (activation_socket < 0) {
        (void)flock(descriptor, LOCK_UN);
        (void)close(descriptor);
        return NULL;
    }
    guard = malloc(sizeof(*guard));
    if (guard == NULL) {
        (void)close(activation_socket);
        (void)unlink(activation_path);
        (void)flock(descriptor, LOCK_UN);
        (void)close(descriptor);
        return NULL;
    }
    guard->descriptor = descriptor;
    guard->activation_socket = activation_socket;
    (void)memcpy(
        guard->activation_path,
        activation_path,
        strlen(activation_path) + 1U
    );
    return guard;
}

bool gdox_instance_guard_activate_existing(void)
{
    char runtime_directory[384];
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct sockaddr_un address;
    socklen_t address_bytes;
    const unsigned char request = 1U;
    unsigned int attempt;
    int descriptor;

    if (!select_runtime_directory(
            runtime_directory, sizeof(runtime_directory)
        )
        || !build_path(
            path, sizeof(path), runtime_directory, activation_suffix
        )
        || !activation_address(path, &address, &address_bytes)) {
        return false;
    }
    descriptor = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (descriptor < 0 || !configure_activation_socket(descriptor)) {
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        return false;
    }
    for (attempt = 0U; attempt < 20U; ++attempt) {
        if (trusted_activation_endpoint(path)
            && sendto(
                descriptor,
                &request,
                sizeof(request),
                0,
                (const struct sockaddr *)&address,
                address_bytes
            ) == (ssize_t)sizeof(request)) {
            (void)close(descriptor);
            return true;
        }
        activation_retry_delay();
    }
    (void)close(descriptor);
    return false;
}

bool gdox_instance_guard_take_activation(gdox_instance_guard *guard)
{
    unsigned char requests[32];
    bool activated = false;

    if (guard == NULL) {
        return false;
    }
    for (;;) {
        const ssize_t received = recv(
            guard->activation_socket,
            requests,
            sizeof(requests),
            0
        );

        if (received >= 0) {
            activated = true;
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        return activated;
    }
}

void gdox_instance_guard_report_conflict(void)
{
    (void)fprintf(
        stderr,
        "GDOX is already running, but its window could not be opened.\n"
    );
}

void gdox_instance_guard_report_failure(void)
{
    (void)fprintf(
        stderr,
        "GDOX could not establish its private desktop-instance lock.\n"
    );
}

void gdox_instance_guard_release(gdox_instance_guard *guard)
{
    if (guard == NULL) {
        return;
    }
    (void)unlink(guard->activation_path);
    (void)close(guard->activation_socket);
    (void)flock(guard->descriptor, LOCK_UN);
    (void)close(guard->descriptor);
    free(guard);
}
