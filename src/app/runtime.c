#include "app/runtime_internal.h"
#include "app/runtime_media.h"
#include "app/optical_monitor.h"
#include "core/hdd_cache.h"
#include "platform/user_storage.h"

#include "gdox/emulator.h"
#include "gdox/optical.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gdox_runtime_copy_text(char *output, size_t capacity, const char *text)
{
    const char *source = text != NULL ? text : "";
    size_t bytes;
    if (output == NULL || capacity == 0U) {
        return;
    }
    bytes = strlen(source);
    if (bytes >= capacity) {
        bytes = capacity - 1U;
    }
    memcpy(output, source, bytes);
    output[bytes] = '\0';
}

/*
 * The user-owned settings carried inside every snapshot. The worker thread
 * publishes session state around them; only the copy pair below may
 * enumerate these fields. A field added to gdox_runtime_snapshot that users
 * control must be added to both functions or it will not persist at all.
 */
static void settings_from_snapshot(
    const gdox_runtime_snapshot *snapshot,
    gdox_preferences *settings
)
{
    gdox_preferences_defaults(settings);
    settings->auto_start = snapshot->auto_start;
    settings->internal_resolution_scale =
        snapshot->internal_resolution_scale;
    settings->display_aspect = snapshot->display_aspect;
    settings->display_fit = snapshot->display_fit;
    settings->fullscreen = snapshot->fullscreen;
    settings->window_width = snapshot->window_width;
    settings->window_height = snapshot->window_height;
    gdox_runtime_copy_text(
        settings->xemu_override,
        sizeof(settings->xemu_override),
        snapshot->xemu_override
    );
    gdox_runtime_copy_text(
        settings->hdd_override,
        sizeof(settings->hdd_override),
        snapshot->hdd_override
    );
    gdox_runtime_copy_text(
        settings->preservation_directory,
        sizeof(settings->preservation_directory),
        snapshot->preservation_directory
    );
}

static void settings_apply(
    gdox_runtime_snapshot *snapshot,
    const gdox_preferences *settings
)
{
    snapshot->auto_start = settings->auto_start;
    snapshot->internal_resolution_scale =
        settings->internal_resolution_scale;
    snapshot->display_aspect = settings->display_aspect;
    snapshot->display_fit = settings->display_fit;
    snapshot->fullscreen = settings->fullscreen;
    snapshot->window_width = settings->window_width;
    snapshot->window_height = settings->window_height;
    gdox_runtime_copy_text(
        snapshot->xemu_override,
        sizeof(snapshot->xemu_override),
        settings->xemu_override
    );
    gdox_runtime_copy_text(
        snapshot->hdd_override,
        sizeof(snapshot->hdd_override),
        settings->hdd_override
    );
    gdox_runtime_copy_text(
        snapshot->preservation_directory,
        sizeof(snapshot->preservation_directory),
        settings->preservation_directory
    );
}

void gdox_runtime_publish(
    gdox_runtime *runtime,
    const gdox_runtime_snapshot *snapshot
)
{
    if (gdox_mutex_lock(&runtime->mutex)) {
        gdox_preferences settings;

        settings_from_snapshot(&runtime->snapshot, &settings);
        runtime->snapshot = *snapshot;
        settings_apply(&runtime->snapshot, &settings);
        gdox_runtime_copy_bundle_status(&runtime->snapshot, &runtime->bundle);
        gdox_mutex_unlock(&runtime->mutex);
    }
}

void gdox_runtime_preferences_from_snapshot(
    const gdox_runtime_snapshot *snapshot,
    gdox_preferences *preferences
)
{
    settings_from_snapshot(snapshot, preferences);
}

static uint32_t take_commands(gdox_runtime *runtime)
{
    uint32_t commands = 0U;
    if (gdox_mutex_lock(&runtime->mutex)) {
        commands = runtime->commands;
        runtime->commands = 0U;
        gdox_mutex_unlock(&runtime->mutex);
    }
    return commands;
}

static bool copy_pending_image_path(
    gdox_runtime *runtime,
    char output[GDOX_EMULATOR_PATH_CAPACITY]
)
{
    if (!gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    gdox_runtime_copy_text(
        output,
        GDOX_EMULATOR_PATH_CAPACITY,
        runtime->pending_image_path
    );
    gdox_mutex_unlock(&runtime->mutex);
    return output[0] != '\0';
}

static bool current_auto_start(gdox_runtime *runtime)
{
    bool enabled = true;
    if (gdox_mutex_lock(&runtime->mutex)) {
        enabled = runtime->snapshot.auto_start;
        gdox_mutex_unlock(&runtime->mutex);
    }
    return enabled;
}

void gdox_runtime_set_controls(
    gdox_runtime_snapshot *snapshot,
    bool has_session,
    bool emulator_running
)
{
    snapshot->can_start =
        has_session && !emulator_running && snapshot->xemu_ready;
    snapshot->can_restart = has_session && snapshot->xemu_ready;
    snapshot->can_close = emulator_running;
    snapshot->can_eject =
        snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC
        && (has_session || snapshot->phase == GDOX_RUNTIME_EMPTY);
    snapshot->can_preserve =
        snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC
        && has_session;
    snapshot->can_cancel_preservation = false;
}

bool gdox_runtime_bundle_complete(
    const gdox_runtime_bundle_status *bundle
)
{
    return bundle->xemu_available
        && bundle->configuration_ready
        && bundle->mcpx_ready
        && bundle->flash_ready
        && bundle->hdd_ready;
}

void gdox_runtime_copy_bundle_status(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_bundle_status *bundle
)
{
    snapshot->xemu_ready = gdox_runtime_bundle_complete(bundle);
    snapshot->bundled_xemu = bundle->bundled;
    snapshot->mcpx_ready = bundle->mcpx_ready;
    snapshot->flash_ready = bundle->flash_ready;
    snapshot->hdd_ready = bundle->hdd_ready;
    snapshot->hdd_cache_reset =
        bundle->hdd_ready && !bundle->custom_hdd;
    gdox_runtime_copy_text(
        snapshot->xemu_executable,
        sizeof(snapshot->xemu_executable),
        bundle->executable
    );
    gdox_runtime_copy_text(
        snapshot->xemu_configuration,
        sizeof(snapshot->xemu_configuration),
        bundle->configuration
    );
    gdox_runtime_copy_text(
        snapshot->mcpx_path,
        sizeof(snapshot->mcpx_path),
        bundle->mcpx
    );
    gdox_runtime_copy_text(
        snapshot->flash_path,
        sizeof(snapshot->flash_path),
        bundle->flash
    );
    gdox_runtime_copy_text(
        snapshot->hdd_path,
        sizeof(snapshot->hdd_path),
        bundle->hdd
    );
}

void gdox_runtime_describe_bundle(
    gdox_runtime_snapshot *snapshot,
    const gdox_runtime_bundle_status *bundle
)
{
    if (gdox_runtime_bundle_complete(bundle)) {
        gdox_runtime_copy_text(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "xemu is ready"
        );
    } else if (!bundle->xemu_available) {
        gdox_runtime_copy_text(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "Bundled xemu runtime is unavailable"
        );
    } else if (!bundle->configuration_ready || !bundle->hdd_ready) {
        gdox_runtime_copy_text(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "xemu private storage could not be prepared"
        );
    } else {
        gdox_runtime_copy_text(
            snapshot->xemu_setup,
            sizeof(snapshot->xemu_setup),
            "Drop MCPX 1.0 and Xbox BIOS files here to finish setup"
        );
    }
}

static bool current_bundle(
    gdox_runtime *runtime,
    gdox_runtime_bundle_status *bundle
)
{
    if (!gdox_mutex_lock(&runtime->mutex)) {
        return false;
    }
    *bundle = runtime->bundle;
    gdox_mutex_unlock(&runtime->mutex);
    return true;
}

static void attention(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *operation,
    const gdox_error *error,
    bool has_session,
    bool emulator_running
)
{
    snapshot->phase = GDOX_RUNTIME_ATTENTION;
    gdox_runtime_copy_text(snapshot->status, sizeof(snapshot->status), "GDOX needs attention");
    (void)snprintf(
        snapshot->notice,
        sizeof(snapshot->notice),
        "%.48s: %.106s",
        operation,
        error->message
    );
    gdox_runtime_set_controls(snapshot, has_session, emulator_running);
    snapshot->preservation_complete = false;
    gdox_runtime_publish(runtime, snapshot);
}

static bool refresh_physical_read_stats(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    bool publish
)
{
    gdox_physical_read_stats stats;
    bool changed;

    if (runtime->exported == NULL
        || !gdox_nbd_physical_read_stats(runtime->exported, &stats)) {
        return false;
    }
    changed = stats.commands != snapshot->physical_read_commands
        || stats.sectors != snapshot->physical_read_sectors
        || stats.bytes != snapshot->physical_read_bytes
        || stats.last_lba != snapshot->physical_last_lba;
    snapshot->physical_read_commands = stats.commands;
    snapshot->physical_read_sectors = stats.sectors;
    snapshot->physical_read_bytes = stats.bytes;
    snapshot->physical_last_lba = stats.last_lba;
    if (changed) {
        (void)fprintf(
            stderr,
            "GDOX: physical optical reads: commands=%" PRIu64
            " sectors=%" PRIu64 " bytes=%" PRIu64
            " last_lba=%" PRIu64 "\n",
            stats.commands,
            stats.sectors,
            stats.bytes,
            stats.last_lba
        );
        (void)fflush(stderr);
        if (publish) {
            gdox_runtime_publish(runtime, snapshot);
        }
    }
    return true;
}

static bool start_emulator(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    gdox_emulator_options options;
    gdox_runtime_bundle_status bundle;

    if (!current_bundle(runtime, &bundle)
        || !gdox_runtime_bundle_complete(&bundle)) {
        gdox_error_set(
            error,
            GDOX_ERROR_NOT_FOUND,
            "xemu setup needs a valid MCPX boot ROM, BIOS, and hard-disk image"
        );
        snapshot->xemu_ready = false;
        return false;
    }
    snapshot->xemu_ready = true;
    if (!bundle.custom_hdd) {
        bool cache_changed;
        if (!gdox_hdd_reset_cache_partitions(
                bundle.hdd,
                &cache_changed,
                error
            )) {
            return false;
        }
        if (cache_changed) {
            (void)fprintf(
                stderr,
                "GDOX: reset Xbox X/Y/Z cache metadata before launch\n"
            );
            (void)fflush(stderr);
        }
    }
    options.executable = bundle.executable;
    options.configuration = bundle.configuration;
    options.console_output = false;
    if (!gdox_mutex_lock(&runtime->mutex)) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not read display settings");
        return false;
    }
    options.internal_resolution_scale =
        runtime->snapshot.internal_resolution_scale;
    options.aspect = runtime->snapshot.display_aspect;
    options.fit = runtime->snapshot.display_fit;
    options.fullscreen = runtime->snapshot.fullscreen;
    options.window_width = runtime->snapshot.window_width;
    options.window_height = runtime->snapshot.window_height;
    gdox_mutex_unlock(&runtime->mutex);
    if (!gdox_emulator_launch(
            &options,
            gdox_nbd_uri(runtime->exported),
            &runtime->emulator,
            error
        )) {
        return false;
    }
    snapshot->phase = GDOX_RUNTIME_PLAYING;
    gdox_runtime_copy_text(snapshot->status, sizeof(snapshot->status), "xemu is running");
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC
            ? "Live physical-disc session is active"
            : "Read-only disc-image session is active"
    );
    gdox_runtime_set_controls(snapshot, true, true);
    gdox_runtime_publish(runtime, snapshot);
    return true;
}

static bool stop_emulator(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    int exit_code;
    if (runtime->emulator == NULL) {
        return true;
    }
    if (!gdox_emulator_stop(
            runtime->emulator,
            3000U,
            &exit_code,
            error
        )) {
        return false;
    }
    gdox_emulator_process_destroy(runtime->emulator);
    runtime->emulator = NULL;
    snapshot->phase = GDOX_RUNTIME_READY;
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC
            ? "Disc ready"
            : "Disc image ready"
    );
    gdox_runtime_set_controls(snapshot, true, false);
    gdox_runtime_publish(runtime, snapshot);
    return true;
}

static bool close_export(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_error *error
)
{
    if (runtime->exported == NULL) {
        return true;
    }
    (void)refresh_physical_read_stats(runtime, snapshot, false);
    if (!gdox_nbd_close(runtime->exported, error)) {
        runtime->exported = NULL;
        return false;
    }
    runtime->exported = NULL;
    snapshot->phase = GDOX_RUNTIME_EMPTY;
    if (snapshot->media_source == GDOX_MEDIA_PHYSICAL_DISC) {
        gdox_runtime_copy_text(
            snapshot->disc,
            sizeof(snapshot->disc),
            "No Xbox disc"
        );
        gdox_runtime_copy_text(
            snapshot->status,
            sizeof(snapshot->status),
            "Waiting for an Xbox disc"
        );
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Drive is ready"
        );
    } else {
        gdox_runtime_copy_text(
            snapshot->status,
            sizeof(snapshot->status),
            "Disc image closed"
        );
        gdox_runtime_copy_text(
            snapshot->notice,
            sizeof(snapshot->notice),
            "Select the image again to reopen it"
        );
    }
    gdox_runtime_set_controls(snapshot, false, false);
    gdox_runtime_publish(runtime, snapshot);
    return true;
}

enum {
    GDOX_OBSERVATION_INTERVAL_TICKS = 5U,
    GDOX_LIVE_DEVICE_INTERVAL_TICKS = 2U,
    GDOX_READ_STATS_INTERVAL_TICKS = 10U,
};

static void publish_missing_drive(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *notice
)
{
    snapshot->phase = GDOX_RUNTIME_EMPTY;
    gdox_runtime_copy_text(
        snapshot->drive,
        sizeof(snapshot->drive),
        "Supported drive unavailable"
    );
    gdox_runtime_copy_text(
        snapshot->disc,
        sizeof(snapshot->disc),
        "No Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Waiting for a supported drive"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        notice
    );
    gdox_runtime_set_controls(snapshot, false, false);
    snapshot->can_eject = false;
    gdox_runtime_publish(runtime, snapshot);
}

static void publish_empty_drive(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot
)
{
    snapshot->phase = GDOX_RUNTIME_EMPTY;
    gdox_runtime_copy_text(
        snapshot->drive,
        sizeof(snapshot->drive),
        "HL-DT-ST DVDRAM GP63EX70 RF02"
    );
    gdox_runtime_copy_text(
        snapshot->disc,
        sizeof(snapshot->disc),
        "No Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Waiting for an Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        "Drive is ready"
    );
    gdox_runtime_set_controls(snapshot, false, false);
    gdox_runtime_publish(runtime, snapshot);
}

static bool prepare_live_session(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    bool force_launch,
    gdox_error *error
)
{
    gdox_runtime_media_info media;

    snapshot->media_source = GDOX_MEDIA_PHYSICAL_DISC;
    snapshot->image_layout = GDOX_MEDIA_IMAGE_NONE;
    snapshot->image_source_sectors = 0U;
    snapshot->image_game_partition_lba = 0U;
    snapshot->disc_image_path[0] = '\0';
    snapshot->physical_read_commands = 0U;
    snapshot->physical_read_sectors = 0U;
    snapshot->physical_read_bytes = 0U;
    snapshot->physical_last_lba = 0U;
    snapshot->phase = GDOX_RUNTIME_PREPARING;
    gdox_runtime_copy_text(
        snapshot->disc,
        sizeof(snapshot->disc),
        "Reading Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Preparing live disc"
    );
    gdox_runtime_set_controls(snapshot, false, false);
    gdox_runtime_publish(runtime, snapshot);
    if (!gdox_runtime_media_open_physical(
            &runtime->exported,
            &media,
            error
        )) {
        attention(
            runtime,
            snapshot,
            "Could not prepare disc",
            error,
            false,
            false
        );
        snapshot->can_start = true;
        gdox_runtime_publish(runtime, snapshot);
        return false;
    }
    gdox_runtime_copy_text(
        snapshot->disc,
        sizeof(snapshot->disc),
        media.title
    );
    snapshot->phase = GDOX_RUNTIME_READY;
    (void)refresh_physical_read_stats(runtime, snapshot, false);
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Disc ready"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        "Live physical-disc session is active"
    );
    gdox_runtime_set_controls(snapshot, true, false);
    gdox_runtime_publish(runtime, snapshot);
    if ((force_launch || current_auto_start(runtime))
        && !start_emulator(runtime, snapshot, error)) {
        attention(
            runtime,
            snapshot,
            "Could not start xemu",
            error,
            true,
            false
        );
    }
    return true;
}

static bool prepare_image_session(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const char *path,
    bool launch,
    gdox_error *error
)
{
    gdox_runtime_media_info media;

    snapshot->media_source = GDOX_MEDIA_DISC_IMAGE;
    snapshot->image_layout = GDOX_MEDIA_IMAGE_NONE;
    snapshot->image_source_sectors = 0U;
    snapshot->image_game_partition_lba = 0U;
    snapshot->physical_read_commands = 0U;
    snapshot->physical_read_sectors = 0U;
    snapshot->physical_read_bytes = 0U;
    snapshot->physical_last_lba = 0U;
    gdox_runtime_copy_text(
        snapshot->disc_image_path,
        sizeof(snapshot->disc_image_path),
        path
    );
    snapshot->phase = GDOX_RUNTIME_PREPARING;
    gdox_runtime_copy_text(
        snapshot->drive,
        sizeof(snapshot->drive),
        "Read-only disc image"
    );
    gdox_runtime_copy_text(
        snapshot->disc,
        sizeof(snapshot->disc),
        "Reading Xbox disc image"
    );
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Preparing disc image"
    );
    gdox_runtime_set_controls(snapshot, false, false);
    gdox_runtime_publish(runtime, snapshot);
    if (!gdox_runtime_media_open_image(
            path,
            &runtime->exported,
            &media,
            error
        )) {
        attention(
            runtime,
            snapshot,
            "Could not open disc image",
            error,
            false,
            false
        );
        snapshot->can_start = snapshot->xemu_ready;
        gdox_runtime_publish(runtime, snapshot);
        return false;
    }
    snapshot->image_layout = media.image_layout;
    snapshot->image_source_sectors = media.source_sectors;
    snapshot->image_game_partition_lba = media.game_partition_lba;
    gdox_runtime_copy_text(
        snapshot->disc,
        sizeof(snapshot->disc),
        media.title
    );
    (void)snprintf(
        snapshot->drive,
        sizeof(snapshot->drive),
        "Disc image · %s",
        gdox_runtime_media_image_layout_name(media.image_layout)
    );
    snapshot->phase = GDOX_RUNTIME_READY;
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Disc image ready"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        "Read-only disc-image session is active"
    );
    gdox_runtime_set_controls(snapshot, true, false);
    gdox_runtime_publish(runtime, snapshot);
    if (launch && !start_emulator(runtime, snapshot, error)) {
        attention(
            runtime,
            snapshot,
            "Could not start xemu",
            error,
            true,
            false
        );
    }
    return true;
}

static void select_physical_source(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    gdox_optical_monitor *monitor
)
{
    snapshot->media_source = GDOX_MEDIA_PHYSICAL_DISC;
    snapshot->image_layout = GDOX_MEDIA_IMAGE_NONE;
    snapshot->image_source_sectors = 0U;
    snapshot->image_game_partition_lba = 0U;
    snapshot->disc_image_path[0] = '\0';
    snapshot->phase = GDOX_RUNTIME_DISCOVERING;
    gdox_runtime_copy_text(
        snapshot->drive,
        sizeof(snapshot->drive),
        "Checking optical drive"
    );
    gdox_runtime_copy_text(
        snapshot->disc,
        sizeof(snapshot->disc),
        "No Xbox disc"
    );
    gdox_runtime_copy_text(
        snapshot->status,
        sizeof(snapshot->status),
        "Looking for a physical disc"
    );
    gdox_runtime_copy_text(
        snapshot->notice,
        sizeof(snapshot->notice),
        "Physical discs are the default source"
    );
    gdox_runtime_set_controls(snapshot, false, false);
    gdox_optical_monitor_retry(monitor);
    gdox_runtime_publish(runtime, snapshot);
}

static void end_disconnected_session(
    gdox_runtime *runtime,
    gdox_runtime_snapshot *snapshot,
    const gdox_error *observation_error,
    gdox_optical_monitor *optical_monitor
)
{
    gdox_error cleanup_error;
    gdox_error notice;

    if (runtime->emulator != NULL) {
        (void)stop_emulator(runtime, snapshot, &cleanup_error);
    }
    if (runtime->exported != NULL) {
        (void)close_export(runtime, snapshot, &cleanup_error);
    }
    if (observation_error != NULL && gdox_error_is_set(observation_error)) {
        (void)snprintf(
            notice.message,
            sizeof(notice.message),
            "physical drive status failed: %.345s",
            observation_error->message
        );
        notice.code = GDOX_ERROR_TRANSPORT;
    } else {
        gdox_error_set(
            &notice,
            GDOX_ERROR_NOT_FOUND,
            "the physical optical drive was disconnected; xemu was stopped"
        );
    }
    gdox_optical_monitor_block(optical_monitor);
    attention(
        runtime,
        snapshot,
        "Live session ended",
        &notice,
        false,
        false
    );
}

static void runtime_thread(void *raw_runtime)
{
    gdox_runtime *runtime = raw_runtime;
    gdox_runtime_snapshot snapshot = {0};
    gdox_optical_monitor optical_monitor;
    uint32_t observation_delay = 0U;
    uint32_t live_device_delay = 0U;
    uint32_t read_stats_delay = 0U;
    uint32_t media_delay = 0U;
    uint32_t missing_checks = 0U;
    bool force_launch = false;
    gdox_error error;

    gdox_optical_monitor_initialize(&optical_monitor);
    gdox_runtime_copy_snapshot(runtime, &snapshot);
    while (!atomic_load_explicit(&runtime->stopping, memory_order_acquire)) {
        const uint32_t commands = take_commands(runtime);

        if ((commands & GDOX_RUNTIME_COMMAND_USE_PHYSICAL) != 0U) {
            bool ready = true;
            runtime->preservation_hold = false;
            if (runtime->emulator != NULL) {
                ready = stop_emulator(runtime, &snapshot, &error);
            }
            if (ready && runtime->exported != NULL) {
                ready = close_export(runtime, &snapshot, &error);
            }
            if (ready) {
                select_physical_source(
                    runtime,
                    &snapshot,
                    &optical_monitor
                );
                observation_delay = 0U;
            } else {
                attention(
                    runtime,
                    &snapshot,
                    "Could not switch to the physical disc",
                    &error,
                    runtime->exported != NULL,
                    runtime->emulator != NULL
                );
            }
            gdox_sleep_ms(100U);
            continue;
        }
        if ((commands & GDOX_RUNTIME_COMMAND_OPEN_IMAGE) != 0U) {
            char image_path[GDOX_EMULATOR_PATH_CAPACITY];
            gdox_error_clear(&error);
            bool ready = copy_pending_image_path(runtime, image_path);

            runtime->preservation_hold = false;
            if (ready && runtime->emulator != NULL) {
                ready = stop_emulator(runtime, &snapshot, &error);
            }
            if (ready && runtime->exported != NULL) {
                ready = close_export(runtime, &snapshot, &error);
            }
            if (ready) {
                (void)prepare_image_session(
                    runtime,
                    &snapshot,
                    image_path,
                    true,
                    &error
                );
            } else {
                if (!gdox_error_is_set(&error)) {
                    gdox_error_set(
                        &error,
                        GDOX_ERROR_INVALID_ARGUMENT,
                        "the selected image path is unavailable"
                    );
                }
                attention(
                    runtime,
                    &snapshot,
                    "Could not switch to the disc image",
                    &error,
                    runtime->exported != NULL,
                    runtime->emulator != NULL
                );
            }
            gdox_sleep_ms(100U);
            continue;
        }
        if ((commands & GDOX_RUNTIME_COMMAND_PRESERVE) != 0U) {
            bool ready = true;
            if (runtime->emulator != NULL) {
                ready = stop_emulator(runtime, &snapshot, &error);
            }
            if (ready && runtime->exported != NULL) {
                ready = close_export(runtime, &snapshot, &error);
            }
            if (ready) {
                if (!gdox_runtime_run_preservation(runtime, &snapshot, &error)) {
                    attention(
                        runtime,
                        &snapshot,
                        "Could not preserve disc",
                        &error,
                        false,
                        false
                    );
                    observation_delay = GDOX_OBSERVATION_INTERVAL_TICKS;
                } else {
                    observation_delay = 0U;
                }
            } else {
                attention(
                    runtime,
                    &snapshot,
                    "Could not close the live session",
                    &error,
                    runtime->exported != NULL,
                    runtime->emulator != NULL
                );
            }
            gdox_sleep_ms(100U);
            continue;
        }
        if ((commands & GDOX_RUNTIME_COMMAND_APPLY_DISPLAY) != 0U
            && runtime->exported != NULL
            && runtime->emulator != NULL) {
            if (!stop_emulator(runtime, &snapshot, &error)
                || !start_emulator(runtime, &snapshot, &error)) {
                attention(
                    runtime,
                    &snapshot,
                    "Could not apply display settings",
                    &error,
                    true,
                    runtime->emulator != NULL
                );
            }
        }

        if (runtime->exported == NULL) {
            if (snapshot.media_source == GDOX_MEDIA_DISC_IMAGE) {
                if ((commands & GDOX_RUNTIME_COMMAND_START) != 0U
                    && snapshot.disc_image_path[0] != '\0') {
                    (void)prepare_image_session(
                        runtime,
                        &snapshot,
                        snapshot.disc_image_path,
                        true,
                        &error
                    );
                }
                gdox_sleep_ms(100U);
                continue;
            }
            if (runtime->preservation_hold) {
                if ((commands & GDOX_RUNTIME_COMMAND_EJECT) != 0U) {
                    runtime->preservation_hold = false;
                    if (!gdox_optical_eject_gp63(&error)) {
                        attention(runtime, &snapshot, "Eject failed", &error, false, false);
                    }
                    observation_delay = GDOX_OBSERVATION_INTERVAL_TICKS;
                    gdox_optical_monitor_block(&optical_monitor);
                    gdox_sleep_ms(100U);
                    continue;
                } else if ((commands & GDOX_RUNTIME_COMMAND_START) != 0U) {
                    runtime->preservation_hold = false;
                    force_launch = true;
                    gdox_optical_monitor_retry(&optical_monitor);
                    observation_delay = 0U;
                } else {
                    gdox_sleep_ms(100U);
                    continue;
                }
            }
            if ((commands & GDOX_RUNTIME_COMMAND_EJECT) != 0U) {
                if (!gdox_optical_eject_gp63(&error)) {
                    attention(runtime, &snapshot, "Eject failed", &error, false, false);
                }
                gdox_optical_monitor_block(&optical_monitor);
                observation_delay = 0U;
            }
            if ((commands & GDOX_RUNTIME_COMMAND_START) != 0U) {
                force_launch = true;
                gdox_optical_monitor_retry(&optical_monitor);
                observation_delay = 0U;
            }
            if (observation_delay == 0U) {
                gdox_optical_presence presence = {0};
                gdox_runtime_bundle_status bundle;

                if (current_bundle(runtime, &bundle)) {
                    gdox_runtime_copy_bundle_status(&snapshot, &bundle);
                }
                if (!gdox_optical_observe_gp63(&presence, &error)) {
                    (void)gdox_optical_monitor_observe(
                        &optical_monitor,
                        &presence
                    );
                    publish_missing_drive(runtime, &snapshot, error.message);
                } else if (!presence.drive_present) {
                    (void)gdox_optical_monitor_observe(
                        &optical_monitor,
                        &presence
                    );
                    publish_missing_drive(
                        runtime,
                        &snapshot,
                        "Connect the supported USB optical drive"
                    );
                } else if (presence.media_status_known
                    && !presence.media_present) {
                    (void)gdox_optical_monitor_observe(
                        &optical_monitor,
                        &presence
                    );
                    publish_empty_drive(runtime, &snapshot);
                } else {
                    if (gdox_optical_monitor_observe(
                            &optical_monitor,
                            &presence
                        )) {
                        if (!prepare_live_session(
                                runtime,
                                &snapshot,
                                force_launch,
                                &error
                            )) {
                            gdox_optical_monitor_fail(&optical_monitor);
                        }
                        force_launch = false;
                    } else if (gdox_optical_monitor_is_armed(
                            &optical_monitor
                        )) {
                        snapshot.phase = GDOX_RUNTIME_DISCOVERING;
                        gdox_runtime_copy_text(
                            snapshot.drive,
                            sizeof(snapshot.drive),
                            "HL-DT-ST DVDRAM GP63EX70 RF02"
                        );
                        gdox_runtime_copy_text(
                            snapshot.status,
                            sizeof(snapshot.status),
                            "Waiting for the disc to become ready"
                        );
                        gdox_runtime_publish(runtime, &snapshot);
                    } else {
                        snapshot.can_start = true;
                        gdox_runtime_publish(runtime, &snapshot);
                    }
                }
                observation_delay = GDOX_OBSERVATION_INTERVAL_TICKS;
            } else {
                --observation_delay;
            }
        } else {
            bool emulator_running = runtime->emulator != NULL;

            const bool physical =
                snapshot.media_source == GDOX_MEDIA_PHYSICAL_DISC;

            if (physical && read_stats_delay == 0U) {
                (void)refresh_physical_read_stats(runtime, &snapshot, true);
                read_stats_delay = GDOX_READ_STATS_INTERVAL_TICKS;
            } else if (physical) {
                --read_stats_delay;
            }
            if (physical && runtime->emulator != NULL
                && live_device_delay == 0U) {
                bool drive_connected = false;
                gdox_error observation_error;
                const bool observed = gdox_optical_gp63_connected(
                    &drive_connected,
                    &observation_error
                );

                if (!observed || !drive_connected) {
                    end_disconnected_session(
                        runtime,
                        &snapshot,
                        observed ? NULL : &observation_error,
                        &optical_monitor
                    );
                    observation_delay = GDOX_OBSERVATION_INTERVAL_TICKS;
                    gdox_sleep_ms(100U);
                    continue;
                }
                live_device_delay = GDOX_LIVE_DEVICE_INTERVAL_TICKS;
            } else if (physical && runtime->emulator != NULL) {
                --live_device_delay;
            } else {
                live_device_delay = 0U;
            }
            if (physical
                && (commands & GDOX_RUNTIME_COMMAND_EJECT) != 0U) {
                if (!stop_emulator(runtime, &snapshot, &error)
                    || !close_export(runtime, &snapshot, &error)
                    || !gdox_optical_eject_gp63(&error)) {
                    attention(runtime, &snapshot, "Could not eject disc", &error, false, false);
                }
                gdox_optical_monitor_block(&optical_monitor);
                observation_delay = GDOX_OBSERVATION_INTERVAL_TICKS;
                gdox_sleep_ms(100U);
                continue;
            }
            if ((commands & GDOX_RUNTIME_COMMAND_CLOSE) != 0U
                && !stop_emulator(runtime, &snapshot, &error)) {
                attention(
                    runtime,
                    &snapshot,
                    "Could not close xemu",
                    &error,
                    true,
                    emulator_running
                );
            }
            if ((commands & GDOX_RUNTIME_COMMAND_RESTART) != 0U) {
                if (stop_emulator(runtime, &snapshot, &error)) {
                    if (!start_emulator(runtime, &snapshot, &error)) {
                        attention(
                            runtime,
                            &snapshot,
                            "Could not restart xemu",
                            &error,
                            true,
                            false
                        );
                    }
                } else {
                    attention(runtime, &snapshot, "Could not restart xemu", &error, true, true);
                }
            } else if ((commands & GDOX_RUNTIME_COMMAND_START) != 0U && runtime->emulator == NULL
                && !start_emulator(runtime, &snapshot, &error)) {
                attention(runtime, &snapshot, "Could not start xemu", &error, true, false);
            }

            if (runtime->emulator != NULL) {
                bool running;
                int exit_code;
                if (!gdox_emulator_poll(
                        runtime->emulator,
                        &running,
                        &exit_code,
                        &error
                    )) {
                    attention(runtime, &snapshot, "Could not monitor xemu", &error, true, true);
                } else if (!running) {
                    gdox_emulator_process_destroy(runtime->emulator);
                    runtime->emulator = NULL;
                    snapshot.phase = GDOX_RUNTIME_READY;
                    gdox_runtime_copy_text(
                        snapshot.status,
                        sizeof(snapshot.status),
                        physical ? "Disc ready" : "Disc image ready"
                    );
                    if (exit_code != 0) {
                        (void)snprintf(
                            snapshot.notice,
                            sizeof(snapshot.notice),
                            "xemu exited with status %d",
                            exit_code
                        );
                    }
                    gdox_runtime_set_controls(&snapshot, true, false);
                    gdox_runtime_publish(runtime, &snapshot);
                }
            }
            if (gdox_nbd_runtime_error(runtime->exported, &error)) {
                attention(
                    runtime,
                    &snapshot,
                    physical ? "Live disc failed" : "Disc image failed",
                    &error,
                    true,
                    runtime->emulator != NULL
                );
            }
            if (!physical) {
                missing_checks = 0U;
                media_delay = 0U;
            } else if (runtime->emulator != NULL) {
                /*
                 * Do not interleave TEST UNIT READY with the optical reads
                 * serving xemu. An uncached read reports removal immediately;
                 * resume explicit media polling after xemu exits.
                 */
                missing_checks = 0U;
                media_delay = 0U;
            } else if (media_delay == 0U) {
                if (gdox_nbd_media_present(runtime->exported)) {
                    missing_checks = 0U;
                } else {
                    ++missing_checks;
                }
                media_delay = 10U;
            } else {
                --media_delay;
            }
            if (missing_checks >= 3U) {
                (void)stop_emulator(runtime, &snapshot, &error);
                if (!close_export(runtime, &snapshot, &error)) {
                    attention(
                        runtime,
                        &snapshot,
                        "Could not close removed disc",
                        &error,
                        false,
                        false
                    );
                }
                missing_checks = 0U;
                gdox_optical_monitor_block(&optical_monitor);
                observation_delay = GDOX_OBSERVATION_INTERVAL_TICKS;
            }
        }
        gdox_sleep_ms(100U);
    }

    if (runtime->emulator != NULL) {
        (void)stop_emulator(runtime, &snapshot, &error);
    }
    if (runtime->exported != NULL) {
        (void)close_export(runtime, &snapshot, &error);
    }
}

static void apply_default_preservation_directory(
    gdox_preferences *preferences
)
{
    char path[GDOX_STORAGE_PATH_CAPACITY];
    gdox_error error;

    if (preferences->preservation_directory[0] != '\0') {
        return;
    }
    if (gdox_user_data_path("preservations", path, &error)
        && gdox_storage_ensure_directory(path, &error)) {
        gdox_runtime_copy_text(
            preferences->preservation_directory,
            sizeof(preferences->preservation_directory),
            path
        );
    }
}

gdox_runtime *gdox_runtime_create(void)
{
    gdox_runtime *runtime = calloc(1U, sizeof(*runtime));
    gdox_preferences preferences;
    gdox_error preferences_error;
    gdox_error bundle_error;
    bool bundle_prepared;

    if (runtime == NULL || !gdox_mutex_init(&runtime->mutex)) {
        free(runtime);
        return NULL;
    }
    if (!gdox_preferences_load(&preferences, &preferences_error)) {
        gdox_preferences_defaults(&preferences);
    }
    apply_default_preservation_directory(&preferences);
    bundle_prepared = gdox_runtime_bundle_prepare(
        preferences.xemu_override,
        preferences.hdd_override,
        &runtime->bundle,
        &bundle_error
    );
    runtime->snapshot.phase = GDOX_RUNTIME_DISCOVERING;
    runtime->snapshot.auto_start = preferences.auto_start;
    runtime->snapshot.internal_resolution_scale =
        preferences.internal_resolution_scale;
    runtime->snapshot.display_aspect = preferences.display_aspect;
    runtime->snapshot.display_fit = preferences.display_fit;
    runtime->snapshot.fullscreen = preferences.fullscreen;
    runtime->snapshot.window_width = preferences.window_width;
    runtime->snapshot.window_height = preferences.window_height;
    gdox_runtime_copy_text(
        runtime->snapshot.xemu_override,
        sizeof(runtime->snapshot.xemu_override),
        preferences.xemu_override
    );
    gdox_runtime_copy_text(
        runtime->snapshot.hdd_override,
        sizeof(runtime->snapshot.hdd_override),
        preferences.hdd_override
    );
    gdox_runtime_copy_text(
        runtime->snapshot.preservation_directory,
        sizeof(runtime->snapshot.preservation_directory),
        preferences.preservation_directory
    );
    gdox_runtime_copy_bundle_status(&runtime->snapshot, &runtime->bundle);
    gdox_runtime_describe_bundle(&runtime->snapshot, &runtime->bundle);
    if (!bundle_prepared) {
        (void)snprintf(
            runtime->snapshot.xemu_setup,
            sizeof(runtime->snapshot.xemu_setup),
            "xemu setup: %.145s",
            bundle_error.message
        );
    }
    gdox_runtime_copy_text(
        runtime->snapshot.drive,
        sizeof(runtime->snapshot.drive),
        "Checking optical drive"
    );
    gdox_runtime_copy_text(runtime->snapshot.disc, sizeof(runtime->snapshot.disc), "No Xbox disc");
    gdox_runtime_copy_text(
        runtime->snapshot.status,
        sizeof(runtime->snapshot.status),
        "Starting GDOX"
    );
    gdox_runtime_copy_text(
        runtime->snapshot.notice,
        sizeof(runtime->snapshot.notice),
        "Native runtime is initializing"
    );
    atomic_init(&runtime->stopping, false);
    atomic_init(&runtime->preservation_cancelled, false);
    if (!gdox_thread_start(&runtime->thread, runtime_thread, runtime)) {
        gdox_mutex_destroy(&runtime->mutex);
        free(runtime);
        return NULL;
    }
    runtime->thread_started = true;
    return runtime;
}
