#include "platform/gp63_gamecube_device.h"
#include "platform/gp63_gamecube_source.h"
#include "platform/gp63_sg_linux.h"

static bool move_transport(void *context, gdox_scsi_transport *output,
                           gdox_error *error)
{
    gdox_scsi_transport *transport = context;
    gdox_error_clear(error);
    *output = *transport;
    *transport = (gdox_scsi_transport){0};
    return true;
}

bool gdox_gp63_gamecube_open_selected(const gdox_optical_device *device,
    gdox_sector_source *source, bool *candidate, gdox_error *error)
{
    static const uint8_t configuration[10] = {0x46U, 2U, 0U, 0U, 0U, 0U, 0U, 0U, 8U, 0U};
    gdox_scsi_transport transport = {0};
    uint8_t reply[8] = {0};
    size_t bytes = 0U;
    gdox_error cleanup;
    bool success;

    gdox_error_clear(error);
    if (device == NULL || source == NULL || candidate == NULL
        || gdox_source_is_valid(source)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "A selected GP63 and empty source are required");
        return false;
    }
    *candidate = false;
    if (!gdox_gp63_sg_linux_open_device(device, &transport, error)) {
        /* SG is optional for existing Xbox support. Never fall through after
         * an I/O/ownership failure, which could detach another reader. */
        if (error->code == GDOX_ERROR_NOT_FOUND || error->code == GDOX_ERROR_UNSUPPORTED) {
            gdox_error_clear(error);
            return true;
        }
        return false;
    }
    success = gdox_scsi_command_in(&transport, "optical media profile", configuration,
        sizeof(configuration), reply, sizeof(reply), 10000U, &bytes, error);
    if (success && (bytes != sizeof(reply)
        || (reply[0] == 0U && reply[1] == 0U && reply[2] == 0U && reply[3] < 4U))) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "optical media profile response is incomplete");
        success = false;
    }
    if (success && reply[6] == 0U && reply[7] == 0U) {
        *candidate = true;
        success = gdox_gp63_gamecube_stream_source_open(move_transport, &transport, source, error);
    }
    if (gdox_scsi_transport_is_valid(&transport)
        && !gdox_scsi_transport_close(&transport, &cleanup)) {
        *error = cleanup;
        return false;
    }
    return success;
}
