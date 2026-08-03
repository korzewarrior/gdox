#ifndef GDOX_X360_H
#define GDOX_X360_H

#include "gdox/disc.h"
#include "gdox/error.h"
#include "gdox/source.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Byte offsets recognized by Xenia's disc-image reader. */
typedef enum gdox_x360_image_layout {
    GDOX_X360_IMAGE_LAYOUT_NONE = 0,
    GDOX_X360_IMAGE_LAYOUT_PARTITION,
    GDOX_X360_IMAGE_LAYOUT_FB20,
    GDOX_X360_IMAGE_LAYOUT_20600,
    GDOX_X360_IMAGE_LAYOUT_02080000,
    GDOX_X360_IMAGE_LAYOUT_0FD90000
} gdox_x360_image_layout;

typedef enum gdox_x360_executable_kind {
    GDOX_X360_EXECUTABLE_NONE = 0,
    GDOX_X360_EXECUTABLE_XEX1,
    GDOX_X360_EXECUTABLE_XEX2
} gdox_x360_executable_kind;

#define GDOX_X360_EXECUTABLE_NAME_CAPACITY 256U

typedef struct gdox_x360_execution_info {
    bool valid;
    uint32_t media_id;
    uint32_t title_id;
    uint8_t platform;
    uint8_t executable_type;
    uint8_t disc_number;
    uint8_t disc_count;
} gdox_x360_execution_info;

typedef struct gdox_x360_disc_info {
    gdox_x360_image_layout layout;
    gdox_x360_executable_kind executable;
    uint64_t source_bytes;
    uint64_t game_offset_bytes;
    uint32_t root_directory_sector;
    uint32_t root_directory_size;
    char launch_executable[GDOX_X360_EXECUTABLE_NAME_CAPACITY];
    gdox_x360_execution_info execution;
} gdox_x360_disc_info;

/* Exact comparisons for metadata obtained from validated disc probes. */
bool gdox_x360_execution_info_equal(
    const gdox_x360_execution_info *left,
    const gdox_x360_execution_info *right
);
bool gdox_x360_disc_info_equal(
    const gdox_x360_disc_info *left,
    const gdox_x360_disc_info *right
);

/*
 * Validates a random-access view without taking ownership. A GDFX descriptor
 * alone is insufficient: the root must contain a regular default.xex with a
 * structurally valid XEX1 or XEX2 header.
 */
bool gdox_x360_disc_probe(
    gdox_random_disc *disc,
    gdox_x360_disc_info *info,
    gdox_error *error
);

/* Finds and validates a named executable in an already-probed root. */
bool gdox_x360_disc_find_executable(
    gdox_random_disc *disc,
    const gdox_x360_disc_info *info,
    const char *name,
    gdox_x360_executable_kind *kind,
    gdox_x360_execution_info *execution,
    gdox_error *error
);

/*
 * Moves source into a read-only random-access disc and probes it. Once the
 * source has moved, a probe failure closes it. If close preparation fails,
 * `output` remains valid solely so the caller can retry close. Failures before
 * the move leave source untouched.
 */
bool gdox_x360_live_disc_build(
    gdox_sector_source *source,
    gdox_random_disc *output,
    gdox_x360_disc_info *info,
    gdox_error *error
);

const char *gdox_x360_image_layout_name(gdox_x360_image_layout layout);

#ifdef __cplusplus
}
#endif

#endif
