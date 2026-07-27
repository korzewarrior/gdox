#ifndef GDOX_PRESERVE_H
#define GDOX_PRESERVE_H

#include "gdox/evidence.h"
#include "gdox/hash.h"
#include "gdox/security.h"
#include "gdox/source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_PRESERVATION_TITLE_CAPACITY 160U
#define GDOX_PRESERVATION_ID_CAPACITY 64U
#define GDOX_PRESERVATION_PATH_CAPACITY 4096U

#define GDOX_EXPECTED_CRC32 UINT32_C(0x01)
#define GDOX_EXPECTED_MD5 UINT32_C(0x02)
#define GDOX_EXPECTED_SHA1 UINT32_C(0x04)
#define GDOX_EXPECTED_SHA256 UINT32_C(0x08)

typedef enum gdox_preservation_format {
    GDOX_PRESERVATION_XISO_COMPACT = 0,
    GDOX_PRESERVATION_REDUMP
} gdox_preservation_format;

typedef enum gdox_preservation_phase {
    GDOX_PRESERVATION_PREPARING = 0,
    GDOX_PRESERVATION_READING,
    GDOX_PRESERVATION_VERIFYING,
    GDOX_PRESERVATION_FINALIZING
} gdox_preservation_phase;

typedef enum gdox_preservation_status {
    GDOX_PRESERVATION_PLAYABLE_XISO = 0,
    GDOX_PRESERVATION_REDUMP_CANDIDATE,
    GDOX_PRESERVATION_REDUMP_EVIDENCE_COMPLETE
} gdox_preservation_status;

typedef enum gdox_security_map_source {
    GDOX_SECURITY_MAP_AUTHENTICATED_SS = 0,
    GDOX_SECURITY_MAP_CATALOG,
    GDOX_SECURITY_MAP_USER
} gdox_security_map_source;

typedef struct gdox_preservation_map {
    gdox_security_map_source source;
    gdox_security_range ranges[GDOX_XGD1_SECURITY_RANGE_COUNT];
    gdox_hashes expected_hashes;
    uint32_t expected_hash_mask;
    char title[GDOX_PRESERVATION_TITLE_CAPACITY];
    char mastering_id[GDOX_PRESERVATION_ID_CAPACITY];
} gdox_preservation_map;

typedef struct gdox_preservation_request {
    gdox_preservation_format format;
    const char *output_path;
    bool verify;
    bool keep_partial;
    const gdox_preservation_map *security_map;
} gdox_preservation_request;

typedef struct gdox_preservation_input {
    gdox_sector_source *source;
    uint64_t source_lba_offset;
    const char *title;
    bool title_id_present;
    uint32_t title_id;
    const char *source_description;
    uint64_t media_patches;
    uint64_t output_sectors;
    bool filesystem_inventory_verified;
} gdox_preservation_input;

typedef struct gdox_preservation_progress {
    gdox_preservation_phase phase;
    uint64_t completed_bytes;
    uint64_t total_bytes;
    double bytes_per_second;
    uint64_t unreadable_sectors;
} gdox_preservation_progress;

typedef struct gdox_bad_sector_range {
    uint64_t start_lba;
    uint64_t end_lba;
} gdox_bad_sector_range;

typedef struct gdox_preservation_result {
    gdox_preservation_format format;
    gdox_preservation_status status;
    uint64_t bytes;
    gdox_hashes hashes;
    bool readback_verified;
    int expected_hashes_match;
    uint64_t normalized_security_sectors;
    uint64_t unreadable_sectors;
    gdox_bad_sector_range *unreadable_ranges;
    size_t unreadable_range_count;
    gdox_disc_evidence evidence;
    char manifest_path[GDOX_PRESERVATION_PATH_CAPACITY];
} gdox_preservation_result;

typedef bool (*gdox_preservation_cancelled_fn)(void *context);
typedef void (*gdox_preservation_progress_fn)(
    void *context,
    const gdox_preservation_progress *progress
);

bool gdox_preservation_catalog_match(
    const gdox_disc_evidence *evidence,
    uint64_t sectors,
    gdox_preservation_map *output,
    gdox_error *error
);

bool gdox_preservation_run(
    const gdox_preservation_request *request,
    const gdox_preservation_input *input,
    gdox_preservation_cancelled_fn cancelled,
    gdox_preservation_progress_fn progress,
    void *callback_context,
    gdox_preservation_result *result,
    gdox_error *error
);

void gdox_preservation_result_destroy(gdox_preservation_result *result);

#ifdef __cplusplus
}
#endif

#endif
