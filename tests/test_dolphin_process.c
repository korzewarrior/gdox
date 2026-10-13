#define _POSIX_C_SOURCE 200809L
#include "gdox/dolphin.h"
#include "platform/disc_bridge_linux.h"
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

struct gdox_disc_bridge { int marker; };
static struct gdox_disc_bridge bridge;
static bool bridge_start_ok, bridge_retained, bridge_alive, bridge_close_ok;
static bool child_alive, stop_exits;
static int spawn_error, signal_error, child_status, wait_interrupts;
static unsigned int signals, closes, sleeps, launches;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); ++failures; } } while (0)

int __wrap_posix_spawn(pid_t *pid, const char *path,
    const posix_spawn_file_actions_t *actions, const posix_spawnattr_t *attributes,
    char *const argv[], char *const envp[]);
pid_t __wrap_waitpid(pid_t pid, int *status, int options);
int __wrap_kill(pid_t pid, int sig);
int __wrap_nanosleep(const struct timespec *requested, struct timespec *remaining);

int __wrap_posix_spawn(pid_t *pid, const char *path,
    const posix_spawn_file_actions_t *actions, const posix_spawnattr_t *attributes,
    char *const argv[], char *const envp[])
{
    bool direct = false, dump_disabled = false;
    (void)actions; (void)attributes;
    ++launches;
    CHECK(strcmp(path, "/verified/dolphin") == 0 && envp != NULL);
    CHECK(strcmp(argv[1], "--user") == 0);
    CHECK(strcmp(argv[2], "/private/profile") == 0);
    CHECK(strcmp(argv[4], "/virtual/disc.iso") == 0);
    for (unsigned int i = 0; argv[i] != NULL; ++i) {
        if (strcmp(argv[i], "Dolphin.Core.LoadGameIntoMemory=False") == 0) direct = true;
        if (strcmp(argv[i], "Dolphin.Movie.DumpFrames=False") == 0) dump_disabled = true;
    }
    CHECK(direct && dump_disabled);
    if (spawn_error != 0) return spawn_error;
    *pid = 321; child_alive = true; return 0;
}
pid_t __wrap_waitpid(pid_t pid, int *status, int options)
{
    CHECK(pid == 321 && options == WNOHANG);
    if (wait_interrupts > 0) { --wait_interrupts; errno = EINTR; return -1; }
    if (child_alive) return 0;
    *status = child_status; return pid;
}
int __wrap_kill(pid_t pid, int sig)
{
    CHECK(pid == 321 && sig == SIGTERM); ++signals;
    if (signal_error != 0) { errno = signal_error; return -1; }
    if (stop_exits) child_alive = false;
    return 0;
}
int __wrap_nanosleep(const struct timespec *requested, struct timespec *remaining)
{
    (void)remaining;
    CHECK(requested->tv_sec == 0 && requested->tv_nsec == 50000000L);
    ++sleeps; return 0;
}
bool gdox_disc_bridge_start(const char *root, bool console, const char *uri,
    uint64_t length, gdox_disc_bridge **output, gdox_error *error)
{
    CHECK(strcmp(root, "/private/profile") == 0 && !console);
    CHECK(strcmp(uri, "nbd://127.0.0.1:1234/session") == 0 && length == 1459978240U);
    *output = bridge_retained ? &bridge : NULL;
    if (!bridge_start_ok) gdox_error_set(error, GDOX_ERROR_IO, "bridge failed");
    return bridge_start_ok;
}
const char *gdox_disc_bridge_path(const gdox_disc_bridge *value)
{
    CHECK(value == &bridge); return "/virtual/disc.iso";
}
bool gdox_disc_bridge_alive(gdox_disc_bridge *value, bool *alive, gdox_error *error)
{
    (void)error; CHECK(value == &bridge); *alive = bridge_alive; return true;
}
bool gdox_disc_bridge_close(gdox_disc_bridge *value, gdox_error *error)
{
    CHECK(value == &bridge && !child_alive); ++closes;
    if (!bridge_close_ok) gdox_error_set(error, GDOX_ERROR_IO, "unmount failed");
    return bridge_close_ok;
}
static void reset(void)
{
    bridge_start_ok = bridge_retained = bridge_alive = bridge_close_ok = true;
    child_alive = false; stop_exits = true;
    spawn_error = signal_error = child_status = wait_interrupts = 0;
    signals = closes = sleeps = launches = 0U;
}
static bool launch(gdox_dolphin_process **owner, gdox_error *error)
{
    gdox_dolphin_runtime runtime = {0};
    strcpy(runtime.launcher, "/verified/dolphin");
    return gdox_dolphin_launch(&runtime, "/private/profile",
        "nbd://127.0.0.1:1234/session", 1459978240U, false, owner, error);
}
int main(void)
{
    gdox_dolphin_process *owner = NULL;
    gdox_error error; bool running; int code;
    reset(); CHECK(launch(&owner, &error));
    CHECK(!launch(&owner, &error) && launches == 1U);
    wait_interrupts = 1;
    CHECK(gdox_dolphin_poll(owner, &running, &code, &error) && running);
    CHECK(gdox_dolphin_stop(&owner, &error));
    CHECK(owner == NULL && signals == 1U && closes == 1U);

    reset(); stop_exits = false; CHECK(launch(&owner, &error));
    CHECK(!gdox_dolphin_stop(&owner, &error) && owner != NULL);
    CHECK(!gdox_dolphin_stop(&owner, &error) && owner != NULL);
    CHECK(signals == 1U && sleeps == 200U && closes == 0U);
    child_alive = false; CHECK(gdox_dolphin_stop(&owner, &error));
    CHECK(owner == NULL && signals == 1U && closes == 1U);

    reset(); CHECK(launch(&owner, &error)); signal_error = EPERM;
    CHECK(!gdox_dolphin_stop(&owner, &error) && closes == 0U);
    signal_error = 0; CHECK(gdox_dolphin_stop(&owner, &error));
    CHECK(signals == 2U && owner == NULL);

    reset(); CHECK(launch(&owner, &error)); bridge_alive = false;
    CHECK(!gdox_dolphin_poll(owner, &running, &code, &error));
    CHECK(running && owner != NULL && closes == 0U);
    bridge_close_ok = false;
    CHECK(!gdox_dolphin_stop(&owner, &error) && owner != NULL && !child_alive);
    bridge_close_ok = true; CHECK(gdox_dolphin_stop(&owner, &error));
    CHECK(owner == NULL && signals == 1U && closes == 2U);

    reset(); bridge_start_ok = false; bridge_retained = false;
    CHECK(!launch(&owner, &error) && owner == NULL && launches == 0U);
    bridge_retained = true;
    CHECK(!launch(&owner, &error) && owner != NULL && launches == 0U);
    CHECK(gdox_dolphin_stop(&owner, &error) && owner == NULL);

    reset(); spawn_error = ENOENT;
    CHECK(!launch(&owner, &error) && owner != NULL);
    CHECK(gdox_dolphin_stop(&owner, &error) && closes == 1U && signals == 0U);

    reset(); CHECK(launch(&owner, &error)); child_alive = false; child_status = 7 << 8;
    CHECK(gdox_dolphin_poll(owner, &running, &code, &error) && !running && code == 7);
    CHECK(gdox_dolphin_stop(&owner, &error) && owner == NULL && signals == 0U);
    CHECK(gdox_dolphin_stop(&owner, &error));
    return failures == 0 ? 0 : 1;
}
