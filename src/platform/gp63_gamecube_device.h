#ifndef GDOX_GP63_GAMECUBE_DEVICE_H
#define GDOX_GP63_GAMECUBE_DEVICE_H

#include "gdox/optical.h"

/* Probes only the selected GP63. A nonzero MMC profile falls through without
 * any device-state writes. A candidate must pass decoded GameCube validation. */
bool gdox_gp63_gamecube_open_selected(const gdox_optical_device *device,
    gdox_sector_source *source, bool *candidate, gdox_error *error);

#endif
