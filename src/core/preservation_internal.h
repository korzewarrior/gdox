#ifndef GDOX_CORE_PRESERVATION_INTERNAL_H
#define GDOX_CORE_PRESERVATION_INTERNAL_H

#include "gdox/preserve.h"

bool gdox_preservation_write_bundle(
    const gdox_preservation_request *request,
    const gdox_preservation_input *input,
    const gdox_preservation_map *security_map,
    gdox_preservation_result *result,
    gdox_error *error
);

bool gdox_preservation_sidecars_available(
    const gdox_preservation_request *request,
    const gdox_disc_evidence *evidence,
    bool has_security_map,
    gdox_error *error
);

#endif
