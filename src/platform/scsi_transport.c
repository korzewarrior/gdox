#include "platform/scsi_transport.h"

bool gdox_scsi_transport_is_valid(const gdox_scsi_transport *transport)
{
    return transport != NULL && transport->context != NULL && transport->ops != NULL
        && transport->ops->command_in != NULL && transport->ops->command_out != NULL
        && transport->ops->command_none != NULL
        && transport->ops->reset != NULL && transport->ops->close != NULL;
}

static bool validate_cdb(
    const gdox_scsi_transport *transport,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (!gdox_scsi_transport_is_valid(transport) || name == NULL || cdb == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "transport, command name, and CDB are required");
        return false;
    }
    if (cdb_bytes == 0U || cdb_bytes > 16U) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "SCSI CDB must contain 1 to 16 bytes");
        return false;
    }
    return true;
}

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
)
{
    if (!validate_cdb(transport, name, cdb, cdb_bytes, error)
        || output == NULL || output_bytes == 0U || transferred == NULL) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "data-in output and transfer count are required");
        }
        return false;
    }
    *transferred = 0U;
    return transport->ops->command_in(
        transport->context,
        name,
        cdb,
        cdb_bytes,
        output,
        output_bytes,
        timeout_ms,
        transferred,
        error
    );
}

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
)
{
    if (!validate_cdb(transport, name, cdb, cdb_bytes, error)
        || input == NULL || input_bytes == 0U || transferred == NULL) {
        if (!gdox_error_is_set(error)) {
            gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "data-out input and transfer count are required");
        }
        return false;
    }
    *transferred = 0U;
    return transport->ops->command_out(
        transport->context,
        name,
        cdb,
        cdb_bytes,
        input,
        input_bytes,
        timeout_ms,
        transferred,
        error
    );
}

bool gdox_scsi_command_none(
    gdox_scsi_transport *transport,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    if (!validate_cdb(transport, name, cdb, cdb_bytes, error)) {
        return false;
    }
    return transport->ops->command_none(
        transport->context,
        name,
        cdb,
        cdb_bytes,
        timeout_ms,
        error
    );
}

bool gdox_scsi_transport_reset(gdox_scsi_transport *transport, gdox_error *error)
{
    gdox_error_clear(error);
    if (!gdox_scsi_transport_is_valid(transport)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "transport is not open");
        return false;
    }
    return transport->ops->reset(transport->context, error);
}

bool gdox_scsi_transport_last_sense(
    const gdox_scsi_transport *transport,
    uint8_t *output,
    size_t output_bytes,
    size_t *sense_bytes
)
{
    if (sense_bytes != NULL) {
        *sense_bytes = 0U;
    }
    if (!gdox_scsi_transport_is_valid(transport)
        || output == NULL || output_bytes == 0U || sense_bytes == NULL
        || transport->ops->last_sense == NULL) {
        return false;
    }
    return transport->ops->last_sense(
        transport->context,
        output,
        output_bytes,
        sense_bytes
    );
}

bool gdox_scsi_transport_prepare_close(
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    gdox_error_clear(error);
    if (!gdox_scsi_transport_is_valid(transport)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "transport is not open");
        return false;
    }
    if (transport->ops->prepare_close == NULL) {
        return true;
    }
    return transport->ops->prepare_close(transport->context, error);
}

bool gdox_scsi_transport_close(gdox_scsi_transport *transport, gdox_error *error)
{
    void *context;
    const gdox_scsi_transport_ops *ops;

    gdox_error_clear(error);
    if (!gdox_scsi_transport_is_valid(transport)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "transport is not open");
        return false;
    }
    if (!gdox_scsi_transport_prepare_close(transport, error)) {
        return false;
    }
    context = transport->context;
    ops = transport->ops;
    transport->context = NULL;
    transport->ops = NULL;
    return ops->close(context, error);
}
