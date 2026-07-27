#include "gdox/preserve.h"

#include <stdio.h>
#include <string.h>

typedef struct gdox_catalog_entry {
    const char *title;
    const char *mastering_id;
    uint32_t pfi_crc32;
    const uint32_t *dmi_crc32;
    size_t dmi_crc32_count;
    uint32_t expected_crc32;
    const char *expected_md5;
    const char *expected_sha1;
    const gdox_security_range *ranges;
} gdox_catalog_entry;

static const uint32_t halo_dmi_crc32[] = {
    UINT32_C(0x8654b27e),
    UINT32_C(0xc7377c49),
    UINT32_C(0xff637773),
};

static const gdox_security_range halo_ranges[GDOX_XGD1_SECURITY_RANGE_COUNT] = {
    {UINT64_C(291292), UINT64_C(295387)},
    {UINT64_C(447992), UINT64_C(452087)},
    {UINT64_C(757224), UINT64_C(761319)},
    {UINT64_C(908256), UINT64_C(912351)},
    {UINT64_C(1068812), UINT64_C(1072907)},
    {UINT64_C(1217808), UINT64_C(1221903)},
    {UINT64_C(1373826), UINT64_C(1377921)},
    {UINT64_C(1611544), UINT64_C(1615639)},
    {UINT64_C(2084124), UINT64_C(2088219)},
    {UINT64_C(2379638), UINT64_C(2383733)},
    {UINT64_C(2527654), UINT64_C(2531749)},
    {UINT64_C(2676146), UINT64_C(2680241)},
    {UINT64_C(2831660), UINT64_C(2835755)},
    {UINT64_C(2989624), UINT64_C(2993719)},
    {UINT64_C(3296914), UINT64_C(3301009)},
    {UINT64_C(3457904), UINT64_C(3461999)},
};

static const gdox_catalog_entry catalog[] = {
    {
        "Halo: Combat Evolved (USA, Rev 2)",
        "MS00409A",
        UINT32_C(0x8fc52135),
        halo_dmi_crc32,
        sizeof(halo_dmi_crc32) / sizeof(halo_dmi_crc32[0]),
        UINT32_C(0x013986d0),
        "523d82be68f267579864331085a56ce8",
        "07326d842e324b35cbd53d4e279aa36199d736a3",
        halo_ranges,
    },
};

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool parse_hex(const char *input, uint8_t *output, size_t output_bytes)
{
    size_t index;
    if (strlen(input) != output_bytes * 2U) {
        return false;
    }
    for (index = 0U; index < output_bytes; ++index) {
        const int high = hex_nibble(input[index * 2U]);
        const int low = hex_nibble(input[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        output[index] = (uint8_t)((unsigned int)high << 4U | (unsigned int)low);
    }
    return true;
}

static bool entry_matches(
    const gdox_catalog_entry *entry,
    uint32_t pfi_crc32,
    uint32_t dmi_crc32
)
{
    size_t index;
    if (entry->pfi_crc32 != pfi_crc32) {
        return false;
    }
    for (index = 0U; index < entry->dmi_crc32_count; ++index) {
        if (entry->dmi_crc32[index] == dmi_crc32) {
            return true;
        }
    }
    return false;
}

bool gdox_preservation_catalog_match(
    const gdox_disc_evidence *evidence,
    uint64_t sectors,
    gdox_preservation_map *output,
    gdox_error *error
)
{
    const gdox_catalog_entry *matched = NULL;
    uint32_t pfi_crc32;
    uint32_t dmi_crc32;
    size_t index;

    gdox_error_clear(error);
    if (evidence == NULL || output == NULL) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "disc evidence and catalog output are required");
        return false;
    }
    memset(output, 0, sizeof(*output));
    if (!evidence->pfi_present || !evidence->dmi_present
        || sectors != GDOX_XGD1_REDUMP_SECTORS) {
        return false;
    }
    pfi_crc32 = gdox_crc32_buffer(evidence->pfi, sizeof(evidence->pfi));
    dmi_crc32 = gdox_crc32_buffer(evidence->dmi, sizeof(evidence->dmi));
    for (index = 0U; index < sizeof(catalog) / sizeof(catalog[0]); ++index) {
        if (entry_matches(&catalog[index], pfi_crc32, dmi_crc32)) {
            if (matched != NULL) {
                gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE, "disc evidence ambiguously matches the catalog");
                return false;
            }
            matched = &catalog[index];
        }
    }
    if (matched == NULL) {
        return false;
    }
    output->source = GDOX_SECURITY_MAP_CATALOG;
    memcpy(output->ranges, matched->ranges, sizeof(output->ranges));
    output->expected_hashes.crc32 = matched->expected_crc32;
    if (!parse_hex(
            matched->expected_md5,
            output->expected_hashes.md5,
            sizeof(output->expected_hashes.md5)
        )
        || !parse_hex(
            matched->expected_sha1,
            output->expected_hashes.sha1,
            sizeof(output->expected_hashes.sha1)
        )) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "built-in preservation catalog contains an invalid hash");
        return false;
    }
    output->expected_hash_mask =
        GDOX_EXPECTED_CRC32 | GDOX_EXPECTED_MD5 | GDOX_EXPECTED_SHA1;
    (void)snprintf(output->title, sizeof(output->title), "%s", matched->title);
    (void)snprintf(
        output->mastering_id,
        sizeof(output->mastering_id),
        "%s",
        matched->mastering_id
    );
    return true;
}
