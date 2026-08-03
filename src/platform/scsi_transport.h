#ifndef GDOX_SCSI_TRANSPORT_H
#define GDOX_SCSI_TRANSPORT_H

#include "gdox/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct gdox_scsi_transport gdox_scsi_transport;

typedef struct gdox_scsi_transport_ops {
    bool (*command_in)(
        void *context,
        const char *name,
        const uint8_t *cdb,
        size_t cdb_bytes,
        uint8_t *output,
        size_t output_bytes,
        uint32_t timeout_ms,
        size_t *transferred,
        gdox_error *error
    );
    bool (*command_out)(
        void *context,
        const char *name,
        const uint8_t *cdb,
        size_t cdb_bytes,
        const uint8_t *input,
        size_t input_bytes,
        uint32_t timeout_ms,
        size_t *transferred,
        gdox_error *error
    );
    bool (*command_none)(
        void *context,
        const char *name,
        const uint8_t *cdb,
        size_t cdb_bytes,
        uint32_t timeout_ms,
        gdox_error *error
    );
    bool (*reset)(void *context, gdox_error *error);
    bool (*close)(void *context, gdox_error *error);
    /*
     * Optional. Completes safety-critical shutdown without consuming the
     * context. The operation must be idempotent; after it succeeds, later
     * calls must also succeed. On failure, the context must remain valid so
     * shutdown can be retried.
     */
    bool (*prepare_close)(void *context, gdox_error *error);
    /* Optional. Returns sense captured by the most recent failed command. */
    bool (*last_sense)(
        const void *context,
        uint8_t *output,
        size_t output_bytes,
        size_t *sense_bytes
    );
} gdox_scsi_transport_ops;

struct gdox_scsi_transport {
    void *context;
    const gdox_scsi_transport_ops *ops;
};

bool gdox_scsi_transport_is_valid(const gdox_scsi_transport *transport);
bool gdox_scsi_command_in(
    gdox_scsi_transport *transport,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint8_t *output,
    size_t output_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
);
bool gdox_scsi_command_out(
    gdox_scsi_transport *transport,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    const uint8_t *input,
    size_t input_bytes,
    uint32_t timeout_ms,
    size_t *transferred,
    gdox_error *error
);
bool gdox_scsi_command_none(
    gdox_scsi_transport *transport,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
);
bool gdox_scsi_transport_reset(gdox_scsi_transport *transport, gdox_error *error);
bool gdox_scsi_transport_last_sense(
    const gdox_scsi_transport *transport,
    uint8_t *output,
    size_t output_bytes,
    size_t *sense_bytes
);
bool gdox_scsi_transport_prepare_close(
    gdox_scsi_transport *transport,
    gdox_error *error
);
/* A failed prepare_close leaves the transport open and eligible for retry. */
bool gdox_scsi_transport_close(gdox_scsi_transport *transport, gdox_error *error);

#endif
