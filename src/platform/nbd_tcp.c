#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "gdox/nbd.h"

#include "platform/nbd_internal.h"
#include "platform/nbd_protocol.h"
#include "platform/nbd_socket.h"
#include "platform/nbd_telemetry.h"
#include "platform/nbd_token.h"
#include "platform/portable_sync.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBD_FLAG_HAS_FLAGS UINT16_C(0x0001)
#define NBD_FLAG_READ_ONLY UINT16_C(0x0002)

void gdox_nbd_export_record_runtime_error(
    gdox_nbd_export *exported,
    const char *message
)
{
    if (!gdox_mutex_lock(&exported->state_mutex)) {
        return;
    }
    if (!exported->runtime_failed) {
        exported->runtime_failed = true;
        gdox_error_set(
            &exported->runtime_error,
            GDOX_ERROR_TRANSPORT,
            message
        );
    }
    gdox_mutex_unlock(&exported->state_mutex);
}

static bool set_active_client(
    gdox_nbd_export *exported,
    gdox_nbd_socket client
)
{
    if (!gdox_mutex_lock(&exported->state_mutex)) {
        return false;
    }
    exported->active = client;
    gdox_mutex_unlock(&exported->state_mutex);
    return true;
}

static bool session_error_is_expected(int code)
{
    return gdox_nbd_socket_error_is_connection_reset(code)
        || gdox_nbd_socket_error_is_protocol(code)
        || gdox_nbd_socket_error_is_permission(code)
        || gdox_nbd_socket_error_is_connection_aborted(code);
}

static void server_thread(void *context)
{
    gdox_nbd_export *exported = context;

    while (!atomic_load_explicit(&exported->stopping, memory_order_acquire)) {
        const gdox_nbd_socket client = gdox_nbd_socket_accept(
            exported->listener
        );

        if (gdox_nbd_socket_is_valid(client)) {
            bool handled;
            int saved_error;

            if (!set_active_client(exported, client)) {
                gdox_nbd_socket_close(client);
                continue;
            }
            handled = gdox_nbd_protocol_handle(exported, client);
            saved_error = gdox_nbd_socket_error();
            (void)set_active_client(exported, GDOX_NBD_INVALID_SOCKET);
            gdox_nbd_socket_close(client);
            if (!handled
                && !atomic_load_explicit(
                    &exported->stopping,
                    memory_order_acquire
                )
                && !session_error_is_expected(saved_error)) {
                char message[GDOX_ERROR_MESSAGE_CAPACITY];
                char detail[160];

                (void)snprintf(
                    message,
                    sizeof(message),
                    "private NBD session failed: %s",
                    gdox_nbd_socket_error_text(saved_error, detail)
                );
                gdox_nbd_export_record_runtime_error(exported, message);
            }
        } else {
            const int saved_error = gdox_nbd_socket_error();

            if (gdox_nbd_socket_error_is_interrupted(saved_error)) {
                continue;
            }
            if (!atomic_load_explicit(
                    &exported->stopping,
                    memory_order_acquire
                )) {
                char message[GDOX_ERROR_MESSAGE_CAPACITY];
                char detail[160];

                (void)snprintf(
                    message,
                    sizeof(message),
                    "private NBD listener failed: %s",
                    gdox_nbd_socket_error_text(saved_error, detail)
                );
                gdox_nbd_export_record_runtime_error(exported, message);
                break;
            }
        }
    }
}

static void release_failed_start(gdox_nbd_export *exported)
{
    gdox_nbd_socket_close(exported->listener);
    gdox_mutex_destroy(&exported->state_mutex);
    if (exported->socket_platform_started) {
        gdox_nbd_socket_platform_stop();
    }
    free(exported);
}

bool gdox_nbd_start(
    gdox_random_disc *disc,
    gdox_nbd_client_access client_access,
    gdox_nbd_export **exported_output,
    gdox_error *error
)
{
    gdox_nbd_export *exported;

    gdox_error_clear(error);
    if (!gdox_disc_is_valid(disc) || exported_output == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "disc and export output are required"
        );
        return false;
    }
    if (client_access != GDOX_NBD_CLIENT_READ_ONLY
        && client_access != GDOX_NBD_CLIENT_WRITE_OPEN) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "NBD client access mode is invalid"
        );
        return false;
    }
    exported = calloc(1U, sizeof(*exported));
    if (exported == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not allocate NBD export"
        );
        return false;
    }
    exported->listener = GDOX_NBD_INVALID_SOCKET;
    exported->active = GDOX_NBD_INVALID_SOCKET;
    exported->export_flags = NBD_FLAG_HAS_FLAGS
        | (client_access == GDOX_NBD_CLIENT_READ_ONLY
            ? NBD_FLAG_READ_ONLY
            : 0U);
    if (!gdox_nbd_socket_platform_start()) {
        free(exported);
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not initialize Windows sockets"
        );
        return false;
    }
    exported->socket_platform_started = true;
    if (!gdox_mutex_init(&exported->state_mutex)) {
        gdox_nbd_socket_platform_stop();
        free(exported);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not initialize NBD state"
        );
        return false;
    }
    if (!gdox_nbd_create_token(exported->export_name, error)) {
        release_failed_start(exported);
        return false;
    }
    exported->listener = gdox_nbd_socket_create();
    if (!gdox_nbd_socket_is_valid(exported->listener)) {
        release_failed_start(exported);
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not create private NBD listener"
        );
        return false;
    }
    if (!gdox_nbd_socket_bind_loopback_listener(
            exported->listener,
            &exported->port
        )) {
        release_failed_start(exported);
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "could not bind private NBD listener"
        );
        return false;
    }
    (void)snprintf(
        exported->uri,
        sizeof(exported->uri),
        "nbd://127.0.0.1:%u/%s",
        (unsigned int)exported->port,
        exported->export_name
    );
    (void)snprintf(
        exported->display_uri,
        sizeof(exported->display_uri),
        "nbd://127.0.0.1:%u/<private-session>",
        (unsigned int)exported->port
    );
    exported->disc = *disc;
    atomic_init(&exported->stopping, false);
    if (!gdox_thread_start(&exported->thread, server_thread, exported)) {
        release_failed_start(exported);
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not start private NBD thread"
        );
        return false;
    }
    exported->thread_started = true;
    disc->context = NULL;
    disc->ops = NULL;
    *exported_output = exported;
    return true;
}

const char *gdox_nbd_uri(const gdox_nbd_export *exported)
{
    return exported != NULL ? exported->uri : NULL;
}

const char *gdox_nbd_display_uri(const gdox_nbd_export *exported)
{
    return exported != NULL ? exported->display_uri : NULL;
}

uint64_t gdox_nbd_length(const gdox_nbd_export *exported)
{
    return exported != NULL ? gdox_disc_length(&exported->disc) : 0U;
}

bool gdox_nbd_media_present(const gdox_nbd_export *exported)
{
    return exported != NULL && gdox_disc_media_present(&exported->disc);
}

bool gdox_nbd_observe_media(
    const gdox_nbd_export *exported,
    gdox_media_observation *output
)
{
    if (output == NULL) {
        return false;
    }
    output->readiness = GDOX_MEDIA_READINESS_UNKNOWN;
    output->generation = 0U;
    output->event = GDOX_MEDIA_EVENT_NONE;
    return exported != NULL
        && gdox_disc_observe_media(&exported->disc, output);
}

bool gdox_nbd_physical_read_stats(
    const gdox_nbd_export *exported,
    gdox_physical_read_stats *output
)
{
    return exported != NULL
        && gdox_disc_physical_read_stats(&exported->disc, output);
}

bool gdox_nbd_get_read_stats(
    const gdox_nbd_export *exported,
    gdox_nbd_read_stats *output
)
{
    gdox_nbd_export *mutable_export = (gdox_nbd_export *)exported;

    if (exported == NULL) {
        if (output != NULL) {
            memset(output, 0, sizeof(*output));
        }
        return false;
    }
    return gdox_nbd_telemetry_snapshot(
        &exported->telemetry,
        &mutable_export->state_mutex,
        output
    );
}

bool gdox_nbd_runtime_error(const gdox_nbd_export *exported, gdox_error *error)
{
    gdox_nbd_export *mutable_export = (gdox_nbd_export *)exported;
    bool failed;

    gdox_error_clear(error);
    if (exported == NULL
        || !gdox_mutex_lock(&mutable_export->state_mutex)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "NBD export is not available"
        );
        return true;
    }
    failed = exported->runtime_failed;
    if (failed) {
        *error = exported->runtime_error;
    }
    gdox_mutex_unlock(&mutable_export->state_mutex);
    return failed;
}

bool gdox_nbd_inspect_disc(
    gdox_nbd_export *exported,
    gdox_nbd_disc_inspector inspector,
    void *context,
    gdox_error *error
)
{
    bool success;

    gdox_error_clear(error);
    if (exported == NULL || inspector == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "NBD export and disc inspector are required"
        );
        return false;
    }
    if (!gdox_mutex_lock(&exported->state_mutex)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INTERNAL,
            "could not lock the private NBD disc"
        );
        return false;
    }
    if (gdox_nbd_socket_is_valid(exported->active)) {
        gdox_mutex_unlock(&exported->state_mutex);
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "private NBD disc is active"
        );
        return false;
    }
    if (exported->runtime_failed) {
        if (error != NULL) {
            *error = exported->runtime_error;
        }
        gdox_mutex_unlock(&exported->state_mutex);
        return false;
    }
    success = inspector(&exported->disc, context, error);
    gdox_mutex_unlock(&exported->state_mutex);
    return success;
}

bool gdox_nbd_close(gdox_nbd_export *exported, gdox_error *error)
{
    gdox_error_clear(error);
    if (exported == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "NBD export is not available"
        );
        return false;
    }
    if (exported->thread_started) {
        atomic_store_explicit(
            &exported->stopping,
            true,
            memory_order_release
        );
        gdox_disc_abort(&exported->disc);
        if (gdox_mutex_lock(&exported->state_mutex)) {
            if (gdox_nbd_socket_is_valid(exported->active)) {
                gdox_nbd_socket_shutdown(exported->active);
            }
            gdox_mutex_unlock(&exported->state_mutex);
        }
        gdox_nbd_socket_wake_loopback(exported->port);
        if (!gdox_thread_join(&exported->thread)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INTERNAL,
                "could not join the private NBD server thread"
            );
            return false;
        }
        exported->thread_started = false;
        gdox_nbd_socket_close(exported->listener);
        exported->listener = GDOX_NBD_INVALID_SOCKET;
    }
    if (gdox_disc_is_valid(&exported->disc)) {
        gdox_error disc_error;

        gdox_error_clear(&disc_error);
        if (!gdox_disc_close(&exported->disc, &disc_error)) {
            if (error != NULL) {
                *error = disc_error;
            }
            return false;
        }
    }
    gdox_mutex_destroy(&exported->state_mutex);
    if (exported->socket_platform_started) {
        gdox_nbd_socket_platform_stop();
    }
    free(exported);
    return true;
}
