#define _POSIX_C_SOURCE 200809L
#include "gdox/optical.h"
#include "gdox/source.h"
#include "platform/gp63_gamecube_source.h"
#include "platform/scsi_transport.h"
#include "platform/portable_sync.h"
#include <stdatomic.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GC_SECTORS_PER_BLOCK UINT32_C(16)
#define GC_FRAME_BYTES 2064U
#define GC_CACHE_RECORD_BYTES 2236U
#define GC_LOGICAL_BLOCK_BYTES \
    (GDOX_LOGICAL_SECTOR_BYTES * GC_SECTORS_PER_BLOCK)
#define GC_CACHE_BYTES UINT32_C(0x0c0000)
#define GC_CACHE_CHUNK_BYTES UINT32_C(0x010000)
#define GC_CACHE_CHUNKS (GC_CACHE_BYTES / GC_CACHE_CHUNK_BYTES)
#define GC_METHOD4_CACHE_READS (GC_CACHE_CHUNKS + 1U)
#define GC_PHYSICAL_BASE UINT32_C(0x30000)
#define GC_METHOD4_DISPLACEMENT_BYTES UINT32_C(65536)
#define GC_METHOD4_STREAM_SECTORS UINT32_C(0x1b)
#define GC_METHOD4_STREAM_BYTES \
    (GC_FRAME_BYTES * GC_METHOD4_STREAM_SECTORS)
#define GC_MAGIC UINT32_C(0xc2339f3d)

typedef struct fake_gc {
    uint8_t cache[GC_CACHE_BYTES];
    uint32_t edc_table[256];
    uint32_t last_prime_lba;
    uint32_t last_displacement_lba;
    uint32_t prime_count;
    uint32_t start_count;
    uint32_t displacement_count;
    uint32_t buffer_count;
    uint32_t write_count;
    uint32_t config_count;
    uint32_t sense_count;
    uint32_t target_record;
    uint32_t configuration_failures;
    uint32_t cache_misses;
    gdox_error_code prime_error;
    uint16_t current_profile;
    uint16_t seed;
    uint8_t prime_sense_key;
    uint8_t prime_asc;
    uint8_t prime_ascq;
    bool protocol_valid;
    bool closed;
    bool wrong_identity;
    bool bad_frame_identity;
    bool bad_edc;
    bool inconsistent_seed;
    bool bad_magic;
    bool wii;
    unsigned wii_fault;
    bool short_buffer;
    bool short_configuration;
    bool prime_sense_inline;
    bool ready_gate_mode;
    bool quality_scan_mode;
    bool split_records;
    bool short_counters;
    bool fail_seek;
    bool outer_parity;
    bool uncorrectable_parity;
    unsigned parity_errors;
    unsigned parity_pattern;
    uint32_t bad_edc_acquisitions;
    uint8_t sense[18];
    bool sense_valid;
    uint8_t admission[4];
    uint32_t admission_writes;
    uint32_t fail_admission_write;
    bool fail_restoration;
    bool disconnected;
    bool presence_unknown;
    bool pause_scan;
    uint32_t pause_lba;
    unsigned pause_outcome;
    atomic_bool scan_entered;
    atomic_bool scan_release;
    atomic_bool capture_finished;
    uint32_t normal_reads;
} fake_gc;

static int failures;

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            (void)fprintf(                                                     \
                stderr,                                                        \
                "%s:%d: check failed: %s\n",                                  \
                __FILE__,                                                      \
                __LINE__,                                                      \
                #expression                                                    \
            );                                                                 \
            ++failures;                                                        \
            return;                                                            \
        }                                                                      \
    } while (false)

static uint32_t read_be_u24(const uint8_t *input)
{
    return (uint32_t)input[0] << 16U
        | (uint32_t)input[1] << 8U
        | (uint32_t)input[2];
}

static uint32_t read_be_u32(const uint8_t *input)
{
    return (uint32_t)input[0] << 24U
        | (uint32_t)input[1] << 16U
        | (uint32_t)input[2] << 8U
        | (uint32_t)input[3];
}

static void put_be_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)(value & 0xffU);
}

static void put_be_u24(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)((value >> 16U) & 0xffU);
    output[1] = (uint8_t)((value >> 8U) & 0xffU);
    output[2] = (uint8_t)(value & 0xffU);
}

static void put_be_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)((value >> 16U) & 0xffU);
    output[2] = (uint8_t)((value >> 8U) & 0xffU);
    output[3] = (uint8_t)(value & 0xffU);
}

static uint32_t make_edc_table_value(uint8_t index)
{
    uint32_t value = (uint32_t)index << 24U;
    unsigned int bit;

    for (bit = 0U; bit < 8U; ++bit) {
        const bool high = (value & UINT32_C(0x80000000)) != 0U;
        value <<= 1U;
        if (high) {
            value ^= UINT32_C(0x80000011);
        }
    }
    return value;
}

static void initialize_fake(fake_gc *fake)
{
    unsigned int index;

    memset(fake, 0, sizeof(*fake));
    atomic_init(&fake->scan_entered, false);
    atomic_init(&fake->scan_release, false);
    atomic_init(&fake->capture_finished, false);
    fake->protocol_valid = true;
    fake->pause_lba = 32U;
    fake->prime_error = GDOX_ERROR_IO;
    fake->prime_sense_key = 0x02U;
    fake->prime_asc = 0x30U;
    fake->prime_sense_inline = true;
    fake->seed = UINT16_C(0x5a6d);
    fake->target_record = 300U;
    for (index = 0U; index < 256U; ++index) {
        fake->edc_table[index] = make_edc_table_value((uint8_t)index);
    }
}

static uint32_t edc_update(
    const fake_gc *fake,
    uint32_t edc,
    uint8_t value
)
{
    return fake->edc_table[(uint8_t)((edc >> 24U) ^ value)]
        ^ (edc << 8U);
}

static uint8_t lfsr_byte(uint16_t *state)
{
    uint8_t value = 0U;
    unsigned int bit;

    for (bit = 0U; bit < 8U; ++bit) {
        const uint16_t output = (uint16_t)(*state >> 14U);
        const uint16_t feedback =
            (uint16_t)(output ^ ((*state >> 10U) & 1U));
        *state = (uint16_t)(((*state << 1U) | feedback) & UINT16_C(0x7fff));
        value = (uint8_t)((value << 1U) | (uint8_t)output);
    }
    return value;
}

static void make_logical_sector(
    uint32_t lba,
    bool bad_magic,
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES]
)
{
    size_t index;

    for (index = 0U; index < GDOX_LOGICAL_SECTOR_BYTES; ++index) {
        output[index] = (uint8_t)(lba + (uint32_t)index);
    }
    if (lba == 0U) {
        output[0] = 'G';
        put_be_u32(
            output + 0x1cU,
            bad_magic ? UINT32_C(0x11223344) : GC_MAGIC
        );
    }
}

static void make_raw_frame(
    fake_gc *fake,
    uint32_t lba,
    uint16_t seed,
    uint8_t output[GC_CACHE_RECORD_BYTES]
)
{
    uint8_t logical[GDOX_LOGICAL_SECTOR_BYTES];
    uint8_t plain[GC_FRAME_BYTES];
    uint16_t lfsr = seed;
    uint32_t edc = 0U;
    size_t index;

    memset(plain, 0, sizeof(plain));
    plain[0] = 0U;
    put_be_u24(plain + 1U, GC_PHYSICAL_BASE + lba);
    make_logical_sector(lba, fake->bad_magic, logical);
    if (fake->wii && lba == 0U) {
        memset(logical, 0, sizeof(logical));
        memcpy(logical, "RTEST1", 6U);
        put_be_u32(logical + 0x18U, UINT32_C(0x5d1c9ea3));
    } else if (fake->wii && lba == 128U) {
        memset(logical, 0, sizeof(logical));
        put_be_u32(logical, fake->wii_fault == 1U ? 33U : 1U);
        put_be_u32(logical + 4U, fake->wii_fault == 2U ? UINT32_MAX : 0x10008U);
        put_be_u32(logical + 0x20U, fake->wii_fault == 4U ? 1U : 0x14000U);
        put_be_u32(logical + 0x24U, fake->wii_fault == 3U ? 1U : 0U);
    } else if (fake->wii && lba == 160U) {
        memset(logical, 0, sizeof(logical));
        put_be_u32(logical + 0x2b8U, fake->wii_fault == 8U ? UINT32_MAX : 0x8000U);
        put_be_u32(logical + 0x2bcU, fake->wii_fault == 5U ? 0x50000000U
            : fake->wii_fault == 6U ? 0U : fake->wii_fault == 7U ? 1U : 0x2000U);
    }
    memcpy(plain + 6U, logical, sizeof(logical));
    for (index = 0U; index < 2060U; ++index) {
        edc = edc_update(fake, edc, plain[index]);
    }
    memset(output, 0xa5, GC_CACHE_RECORD_BYTES);
    memcpy(output, plain, sizeof(plain));
    for (index = 0U; index < GDOX_LOGICAL_SECTOR_BYTES; ++index) {
        output[12U + index] ^= lfsr_byte(&lfsr);
    }
    put_be_u32(output + 2060U, edc);
}

/* Independent polynomial encoder for synthetic DVD outer-parity fixtures. */
static uint8_t fixture_gf_multiply(uint8_t left, uint8_t right)
{
    unsigned a = left;
    unsigned b = right;
    unsigned result = 0U;
    while (b != 0U) {
        if ((b & 1U) != 0U) result ^= a;
        b >>= 1U;
        a <<= 1U;
        if ((a & 256U) != 0U) a ^= 0x11dU;
    }
    return (uint8_t)result;
}

static void add_fixture_outer_parity(uint8_t *raw)
{
    uint8_t generator[17] = {1U};
    uint8_t root = 1U;
    for (unsigned degree = 0U; degree < 16U; ++degree) {
        uint8_t next[17] = {0};
        for (unsigned i = 0U; i <= degree; ++i) {
            next[i] ^= generator[i];
            next[i + 1U] ^= fixture_gf_multiply(generator[i], root);
        }
        memcpy(generator, next, sizeof(generator));
        root = fixture_gf_multiply(root, 2U);
    }
    for (unsigned column = 0U; column < 172U; ++column) {
        uint8_t parity[16] = {0};
        for (unsigned row = 0U; row < 192U; ++row) {
            const uint8_t feedback = parity[0]
                ^ raw[(row / 12U) * GC_CACHE_RECORD_BYTES + (row % 12U) * 172U + column];
            for (unsigned i = 0U; i < 15U; ++i) {
                parity[i] = parity[i + 1U] ^ fixture_gf_multiply(feedback, generator[i + 1U]);
            }
            parity[15] = fixture_gf_multiply(feedback, generator[16]);
        }
        for (unsigned i = 0U; i < 16U; ++i) {
            raw[i * GC_CACHE_RECORD_BYTES + GC_FRAME_BYTES + column] = parity[i];
        }
    }
}

static void populate_cache(fake_gc *fake, uint32_t lba)
{
    const uint16_t seed =
        (uint16_t)(
            fake->seed ^ (uint16_t)(lba / GC_SECTORS_PER_BLOCK)
        );
    uint32_t index;

    memset(fake->cache, 0, sizeof(fake->cache));
    for (index = 0U; index < GC_SECTORS_PER_BLOCK; ++index) {
        const uint16_t frame_seed =
            fake->inconsistent_seed && index == 1U
                ? (uint16_t)(seed ^ UINT16_C(1))
                : seed;

        make_raw_frame(
            fake,
            lba + index,
            frame_seed,
            fake->cache
                + ((size_t)fake->target_record + index)
                    * GC_CACHE_RECORD_BYTES
        );
    }
    if (fake->outer_parity) {
        uint8_t *raw = fake->cache + (size_t)fake->target_record * GC_CACHE_RECORD_BYTES;
        add_fixture_outer_parity(raw);
        static const unsigned columns[7] = {58U, 78U, 79U, 80U, 98U, 99U, 105U};
        for (unsigned i = 0U; i < 7U; ++i) {
            raw[GC_CACHE_RECORD_BYTES + 2U * 172U + columns[i]] ^= (uint8_t)(17U + i);
        }
        if (fake->uncorrectable_parity) {
            for (unsigned i = 0U; i < 8U; ++i) {
                raw[(size_t)(i + 2U) * GC_CACHE_RECORD_BYTES + 2U * 172U + columns[0]] ^= 0x59U;
            }
        }
    }
    if (fake->parity_errors != 0U) {
        uint8_t *raw = fake->cache + (size_t)fake->target_record * GC_CACHE_RECORD_BYTES;
        add_fixture_outer_parity(raw);
        /* Sweep all columns with independently encoded data. Include row 0,
         * the last parity row, and zero S0/S1 cancellation cases. Keep the
         * first six columns' sector identities available for reassembly. */
        for (unsigned col = 6U; col < 172U; ++col) {
            for (unsigned i = 0U; i < fake->parity_errors; ++i) {
                const unsigned row = fake->parity_pattern == 0U
                    ? (i == 0U ? 207U : (i - 1U) * 27U)
                    : (fake->parity_pattern * 13U + col + i * 29U) % 208U;
                const size_t off = row < 192U
                    ? (row / 12U) * GC_CACHE_RECORD_BYTES + (row % 12U) * 172U + col
                    : (row - 192U) * GC_CACHE_RECORD_BYTES + GC_FRAME_BYTES + col;
                uint8_t magnitude = (uint8_t)(1U + (col + i * 37U) % 255U);
                if (col == 58U) magnitude = 0x5aU; /* S0=0 for even counts. */
                if (col == 59U && fake->parity_errors == 2U) {
                    /* e_i = alpha^row_i makes both S1 terms identical. */
                    magnitude = 1U;
                    for (unsigned j = 0U; j < row; ++j) {
                        magnitude = fixture_gf_multiply(magnitude, 2U);
                    }
                }
                raw[off] ^= magnitude;
            }
        }
    }
    if (fake->bad_frame_identity) {
        fake->cache[
            ((size_t)fake->target_record + 1U) * GC_CACHE_RECORD_BYTES + 3U
        ] ^= 1U;
    }
    if (fake->bad_edc) {
        fake->cache[
            (size_t)fake->target_record * GC_CACHE_RECORD_BYTES + 2063U
        ] ^= 1U;
    }
    if (fake->split_records) {
        /* The first half is at the ring's tail, the second at its head. */
        memcpy(fake->cache, fake->cache + ((size_t)fake->target_record + 8U)
            * GC_CACHE_RECORD_BYTES, 8U * GC_CACHE_RECORD_BYTES);
        memset(fake->cache + ((size_t)fake->target_record + 8U)
            * GC_CACHE_RECORD_BYTES, 0, 8U * GC_CACHE_RECORD_BYTES);
    }
}

static void fake_set_sense(fake_gc *fake, gdox_error *error, const char *name,
    uint8_t key, uint8_t asc, uint8_t ascq)
{
    memset(fake->sense, 0, sizeof(fake->sense));
    fake->sense[0] = 0x71U;
    fake->sense[2] = key;
    fake->sense[7] = 10U;
    fake->sense[12] = asc;
    fake->sense[13] = ascq;
    fake->sense_valid = true;
    gdox_error_set(error, GDOX_ERROR_IO, name);
}

static bool fake_last_sense(const void *raw_context, uint8_t *output, size_t capacity, size_t *count)
{
    const fake_gc *fake = raw_context;
    if (!fake->sense_valid || capacity < sizeof(fake->sense)) { return false; }
    memcpy(output, fake->sense, sizeof(fake->sense));
    *count = sizeof(fake->sense);
    return true;
}

static bool fake_command_in(
    void *raw_context,
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
    fake_gc *fake = raw_context;

    fake->sense_valid = false;
    (void)name;
    if (fake->ready_gate_mode && cdb[0] == 0xf1U) {
        const unsigned int index = cdb[4] == 3U && cdb[5] == 0x7eU
            ? 3U : (unsigned int)cdb[5] - 0x38U;
        if (cdb_bytes != 10U || cdb[1] != 2U || output_bytes != 4U
            || index >= 4U || (index != 3U && cdb[4] != 0x85U)) {
            fake->protocol_valid = false;
            gdox_error_set(error, GDOX_ERROR_PROTOCOL, "unexpected admission read");
            return false;
        }
        memset(output, 0, output_bytes);
        output[3] = fake->admission[index];
        *transferred = output_bytes;
        return true;
    }
    if (cdb[0] == 0x46U) {
        if (cdb_bytes != 10U
            || cdb[1] != 0x01U
            || cdb[2] != 0U
            || cdb[3] != 0U
            || cdb[7] != 0U
            || cdb[8] != 8U
            || cdb[9] != 0U
            || output_bytes != 8U) {
            fake->protocol_valid = false;
        }
        ++fake->config_count;
        if (fake->configuration_failures != 0U) {
            --fake->configuration_failures;
            *transferred = 0U;
            gdox_error_set(
                error,
                GDOX_ERROR_TRANSPORT,
                "injected current-profile transport failure"
            );
            return false;
        }
        memset(output, 0, output_bytes);
        put_be_u32(output, UINT32_C(4));
        put_be_u16(output + 6U, fake->current_profile);
        *transferred =
            fake->short_configuration ? output_bytes - 1U : output_bytes;
        return true;
    }
    if (cdb[0] == 0x12U) {
        static const uint8_t inquiry[6] = {
            0x12U, 0U, 0U, 0U, 36U, 0U,
        };

        if (cdb_bytes != sizeof(inquiry)
            || memcmp(cdb, inquiry, sizeof(inquiry)) != 0
            || output_bytes != 36U
            || timeout_ms != UINT32_C(10000)) {
            fake->protocol_valid = false;
        }
        memset(output, ' ', output_bytes);
        memcpy(output + 8U, "HL-DT-ST", 8U);
        memcpy(
            output + 16U,
            fake->wrong_identity ? "DVDRAM GP08NU10" : "DVDRAM GP63EX70",
            16U
        );
        memcpy(
            output + 32U,
            fake->wrong_identity ? "JE01" : "RF02",
            4U
        );
        *transferred = output_bytes;
        return true;
    }
    if (cdb[0] == 0xf3U && fake->quality_scan_mode) {
        static const uint8_t scan[12] = {0xf3U, 0x0eU, 0U, 0U, 0U, 0U,
            0U, 0U, 0x10U, 0U, 0U, 0U};
        if (cdb_bytes != sizeof(scan) || memcmp(cdb, scan, sizeof(scan)) != 0
            || output_bytes != 16U || timeout_ms != UINT32_C(30000)
            || fake->admission[3] != 1U) {
            fake->protocol_valid = false;
        }
        ++fake->prime_count;
        if (fake->pause_scan && fake->last_prime_lba == fake->pause_lba) {
            atomic_store(&fake->scan_entered, true);
            while (!atomic_load(&fake->scan_release)) gdox_sleep_ms(1U);
            if (fake->pause_outcome == 1U) fake->prime_error = GDOX_ERROR_TRANSPORT;
            if (fake->pause_outcome == 2U) fake->cache_misses = 1U;
        }
        if (fake->prime_error == GDOX_ERROR_TRANSPORT) {
            gdox_error_set(error, GDOX_ERROR_TRANSPORT, "injected scan transport fault");
            *transferred = 0U;
            return false;
        }
        if (fake->cache_misses != 0U) {
            --fake->cache_misses;
            memset(fake->cache, 0, sizeof(fake->cache));
        } else {
            populate_cache(fake, fake->last_prime_lba);
        }
        memset(output, 0, output_bytes);
        *transferred = fake->short_counters ? 11U : 12U;
        return true;
    }
    if (cdb[0] == 0xa8U) {
        ++fake->normal_reads;
        const uint32_t lba = read_be_u32(cdb + 2U);
        const uint32_t blocks = read_be_u32(cdb + 6U);

        if (fake->ready_gate_mode) {
            if (cdb_bytes != 12U || cdb[1] != 0U || blocks != 1U
                || cdb[10] != 0x80U || output_bytes != 2048U
                || timeout_ms != UINT32_C(30000) || fake->admission[3] != 1U
                || read_be_u24(fake->admission) != GC_PHYSICAL_BASE + lba) {
                fake->protocol_valid = false;
            }
            fake->last_prime_lba = lba;
            ++fake->prime_count;
            if (fake->cache_misses != 0U) {
                --fake->cache_misses;
                memset(fake->cache, 0, sizeof(fake->cache));
            } else {
                const bool persistent_bad_edc = fake->bad_edc;
                if (fake->bad_edc_acquisitions != 0U) {
                    --fake->bad_edc_acquisitions;
                    fake->bad_edc = true;
                }
                populate_cache(fake, lba);
                fake->bad_edc = persistent_bad_edc;
            }
            *transferred = 0U;
            if (fake->prime_error == GDOX_ERROR_IO) {
                fake_set_sense(fake, error, "Nintendo decoder rejection",
                    fake->prime_sense_key, fake->prime_asc, fake->prime_ascq);
            } else {
                gdox_error_set(error, fake->prime_error, "injected acquisition fault");
            }
            return false;
        }
        if (cdb[1] == 0x08U) {
            if (cdb_bytes != 12U
                || lba % GC_SECTORS_PER_BLOCK != 0U
                || blocks != GC_SECTORS_PER_BLOCK
                || output_bytes != GC_METHOD4_DISPLACEMENT_BYTES
                || cdb[10] != 0U
                || cdb[11] != 0U
                || timeout_ms != UINT32_C(100000)) {
                fake->protocol_valid = false;
            }
            fake->last_displacement_lba = lba;
            ++fake->displacement_count;
            *transferred = 0U;
            fake_set_sense(fake,
                error,
                "injected GameCube displacement result",
                0x02U,
                0x30U,
                0x00U
            );
            return false;
        }
        if (cdb_bytes != 12U
            || cdb[1] != 0U
            || blocks != GC_METHOD4_STREAM_SECTORS
            || lba % GC_SECTORS_PER_BLOCK != 0U
            || output_bytes != GC_METHOD4_STREAM_BYTES
            || cdb[10] != 0x80U
            || cdb[11] != 0U
            || timeout_ms != UINT32_C(100000)) {
            fake->protocol_valid = false;
        }
        fake->last_prime_lba = lba;
        ++fake->prime_count;
        if (fake->cache_misses != 0U) {
            --fake->cache_misses;
            memset(fake->cache, 0, sizeof(fake->cache));
        } else {
            populate_cache(fake, lba);
        }
        if (fake->prime_error == GDOX_ERROR_NONE) {
            memset(output, 0, output_bytes);
            *transferred = output_bytes;
            return true;
        }
        *transferred = 0U;
        if (fake->prime_error == GDOX_ERROR_IO
            && fake->prime_sense_inline) {
            fake_set_sense(fake,
                error,
                "injected GameCube prime result",
                fake->prime_sense_key,
                fake->prime_asc,
                fake->prime_ascq
            );
        } else {
            gdox_error_set(
                error,
                fake->prime_error,
                "injected GameCube prime result"
            );
        }
        return false;
    }
    if (cdb[0] == 0x03U) {
        if (cdb_bytes != 6U
            || cdb[1] != 0U
            || cdb[2] != 0U
            || cdb[3] != 0U
            || cdb[4] != 18U
            || cdb[5] != 0U
            || output_bytes != 18U) {
            fake->protocol_valid = false;
        }
        memset(output, 0, output_bytes);
        output[0] = 0x71U;
        output[2] = fake->prime_sense_key;
        output[7] = 10U;
        output[12] = fake->prime_asc;
        output[13] = fake->prime_ascq;
        ++fake->sense_count;
        *transferred = output_bytes;
        return true;
    }
    if (cdb[0] == 0x3cU) {
        const uint32_t address = read_be_u24(cdb + 3U);
        const uint32_t bytes = read_be_u24(cdb + 6U);
        if (fake->ready_gate_mode) {
            if (cdb_bytes != 12U || cdb[1] != 2U || bytes != output_bytes
                || address + bytes > GC_CACHE_BYTES || output_bytes > GC_CACHE_CHUNK_BYTES
                || memcmp(fake->admission, (uint8_t[4]){0}, 4U) != 0) {
                fake->protocol_valid = false;
                gdox_error_set(error, GDOX_ERROR_PROTOCOL, "cache read before admission restoration");
                return false;
            }
            memcpy(output, fake->cache + address, output_bytes);
            ++fake->buffer_count;
            *transferred = fake->short_buffer ? output_bytes - 1U : output_bytes;
            if (atomic_load(&fake->scan_entered) && address + bytes == GC_CACHE_BYTES)
                atomic_store(&fake->capture_finished, true);
            return true;
        }
        const uint32_t ordinal =
            fake->buffer_count % (GC_CACHE_CHUNKS + 1U);
        const bool method4_cache =
            ordinal == 0U
            && cdb_bytes == 12U
            && address == 0U
            && output_bytes == GC_METHOD4_STREAM_BYTES
            && cdb[10] == 0U
            && cdb[11] == 0U
            && timeout_ms == UINT32_C(30000);
        const bool full_cache_chunk =
            ordinal != 0U
            && cdb_bytes == 12U
            && output_bytes == GC_CACHE_CHUNK_BYTES
            && address
                == (ordinal - 1U) * GC_CACHE_CHUNK_BYTES
            && cdb[10] == 0U
            && cdb[11] == 0U
            && timeout_ms == UINT32_C(100000);
        size_t returned = output_bytes;

        if (cdb[1] != 0x02U
            || cdb[2] != 0U
            || bytes != output_bytes
            || (!method4_cache && !full_cache_chunk)
            || (uint64_t)address + output_bytes > GC_CACHE_BYTES
            || cdb[9] != 0U) {
            fake->protocol_valid = false;
        }
        ++fake->buffer_count;
        if ((uint64_t)address + output_bytes <= GC_CACHE_BYTES) {
            memcpy(output, fake->cache + address, output_bytes);
        } else {
            memset(output, 0, output_bytes);
        }
        if (fake->short_buffer) {
            returned -= 1U;
        }
        *transferred = returned;
        return true;
    }
    fake->protocol_valid = false;
    gdox_error_set(error, GDOX_ERROR_PROTOCOL, "unexpected fake data-in command");
    return false;
}

static bool fake_command_out(
    void *raw_context,
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
    fake_gc *fake = raw_context;

    fake->sense_valid = false;
    (void)name;
    (void)cdb;
    (void)cdb_bytes;
    (void)input;
    (void)input_bytes;
    (void)timeout_ms;
    ++fake->write_count;
    *transferred = 0U;
    gdox_error_set(error, GDOX_ERROR_PROTOCOL, "GameCube source attempted a write");
    return false;
}

static bool fake_command_none(
    void *raw_context,
    const char *name,
    const uint8_t *cdb,
    size_t cdb_bytes,
    uint32_t timeout_ms,
    gdox_error *error
)
{
    fake_gc *fake = raw_context;
    static const uint8_t method4_start[12] = {
        0x1bU, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };

    fake->sense_valid = false;
    (void)name;
    if (fake->ready_gate_mode) {
        if (cdb_bytes == 10U && cdb[0] == 0x2bU && fake->quality_scan_mode) {
            const uint32_t lba = read_be_u32(cdb + 2U);
            uint32_t limit = lba + 1024U;
            if (limit >= (fake->wii ? GDOX_WII_SL_TOTAL_SECTORS : GDOX_GAMECUBE_TOTAL_SECTORS)) {
                limit = (uint32_t)(fake->wii ? GDOX_WII_SL_TOTAL_SECTORS : GDOX_GAMECUBE_TOTAL_SECTORS) - 1U;
            }
            if (cdb[1] != 0U || memcmp(cdb + 6U, (uint8_t[4]){0}, 4U) != 0
                || timeout_ms != UINT32_C(30000)
                || read_be_u24(fake->admission) != GC_PHYSICAL_BASE + limit
                || fake->admission[3] != 1U) {
                fake->protocol_valid = false;
            }
            if (fake->fail_seek) {
                gdox_error_set(error, GDOX_ERROR_TRANSPORT, "injected seek transport fault");
                return false;
            }
            fake->last_prime_lba = lba;
            return true;
        }
        if (cdb_bytes == 6U && memcmp(cdb, (uint8_t[6]){0}, 6U) == 0) {
            return fake->admission[3] == 1U;
        }
        if (cdb_bytes == 10U && cdb[0] == 0xf1U && cdb[1] == 1U) {
            const unsigned int index = cdb[4] == 3U && cdb[5] == 0x7eU
                ? 3U : (unsigned int)cdb[5] - 0x38U;
            if (index >= 4U || (index != 3U && cdb[4] != 0x85U)) {
                fake->protocol_valid = false;
                return false;
            }
            ++fake->admission_writes;
            if (fake->admission_writes == fake->fail_admission_write
                || (fake->fail_restoration && cdb[9] == 0U)) {
                gdox_error_set(error, GDOX_ERROR_TRANSPORT, "injected volatile write fault");
                return false;
            }
            fake->admission[index] = cdb[9];
            return true;
        }
        fake->protocol_valid = false;
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "unexpected ready-gate command");
        return false;
    }
    if (cdb_bytes != sizeof(method4_start)
        || memcmp(cdb, method4_start, sizeof(method4_start)) != 0
        || timeout_ms != UINT32_C(100000)) {
        fake->protocol_valid = false;
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "unexpected fake no-data command"
        );
        return false;
    }
    ++fake->start_count;
    fake_set_sense(fake,
        error,
        "injected GameCube START result",
        0x02U,
        0x30U,
        0x00U
    );
    return false;
}

static bool fake_reset(void *raw_context, gdox_error *error)
{
    (void)raw_context;
    gdox_error_clear(error);
    return true;
}

static bool fake_close(void *raw_context, gdox_error *error)
{
    fake_gc *fake = raw_context;
    fake->closed = true;
    gdox_error_clear(error);
    return true;
}

static bool fake_device_present(const void *raw_context, bool *present, gdox_error *error)
{
    const fake_gc *fake = raw_context;
    if (fake->presence_unknown) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "injected presence timeout");
        return false;
    }
    *present = !fake->disconnected;
    gdox_error_clear(error);
    return true;
}

static const gdox_scsi_transport_ops fake_ops = {
    fake_command_in,
    fake_command_out,
    fake_command_none,
    fake_reset,
    fake_close,
    NULL,
    fake_last_sense,
    fake_device_present,
};

static bool fake_opener(
    void *raw_context,
    gdox_scsi_transport *transport,
    gdox_error *error
)
{
    gdox_error_clear(error);
    transport->context = raw_context;
    transport->ops = &fake_ops;
    return true;
}

static bool open_fake(
    fake_gc *fake,
    uint8_t retries,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_gp63_gamecube_source_open(
        fake_opener,
        fake,
        retries,
        0U,
        source,
        error
    );
}

static bool open_fake_with_timeout(
    fake_gc *fake,
    uint8_t retries,
    uint32_t ready_timeout_ms,
    gdox_sector_source *source,
    gdox_error *error
)
{
    return gdox_gp63_gamecube_source_open(
        fake_opener,
        fake,
        retries,
        ready_timeout_ms,
        source,
        error
    );
}

static void test_successful_read_only_source(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[3U * GDOX_LOGICAL_SECTOR_BYTES];
    uint8_t expected[GDOX_LOGICAL_SECTOR_BYTES];
    uint32_t index;

    initialize_fake(&fake);
    fake.target_record = 300U;
    CHECK(open_fake(&fake, 1U, &source, &error));
    CHECK(gdox_source_sector_count(&source) == GDOX_GAMECUBE_TOTAL_SECTORS);
    CHECK(fake.protocol_valid);
    CHECK(fake.config_count == 0U);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.start_count == 1U);
    CHECK(fake.displacement_count == 1U);
    CHECK(fake.last_displacement_lba == UINT32_C(0x03e0));
    CHECK(fake.sense_count == 0U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(fake.write_count == 0U);
    CHECK(gdox_source_media_present(&source));
    CHECK(fake.config_count == 0U);

    CHECK(gdox_source_read(
        &source,
        0U,
        3U,
        output,
        sizeof(output),
        &error
    ));
    for (index = 0U; index < 3U; ++index) {
        make_logical_sector(index, false, expected);
        CHECK(memcmp(
            output + (size_t)index * GDOX_LOGICAL_SECTOR_BYTES,
            expected,
            sizeof(expected)
        ) == 0);
    }
    CHECK(fake.prime_count == 1U);
    CHECK(fake.start_count == 1U);
    CHECK(fake.displacement_count == 1U);
    CHECK(fake.last_displacement_lba == UINT32_C(0x03e0));
    CHECK(fake.sense_count == 0U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(fake.last_prime_lba == 0U);
    CHECK(gdox_source_read(
        &source,
        0U,
        1U,
        output,
        GDOX_LOGICAL_SECTOR_BYTES,
        &error
    ));
    make_logical_sector(0U, false, expected);
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.start_count == 1U);
    CHECK(fake.displacement_count == 1U);
    CHECK(fake.sense_count == 0U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(!gdox_source_read(
        &source,
        16U,
        1U,
        output,
        GDOX_LOGICAL_SECTOR_BYTES,
        &error
    ));
    CHECK(error.code == GDOX_ERROR_IO);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.start_count == 1U);
    CHECK(fake.displacement_count == 1U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    fake.current_profile = UINT16_C(0x0010);
    CHECK(gdox_source_media_present(&source));
    CHECK(fake.config_count == 0U);
    CHECK(fake.protocol_valid);
    CHECK(fake.write_count == 0U);
    CHECK(gdox_source_close(&source, &error));
    CHECK(fake.closed);
    CHECK(fake.write_count == 0U);
}

static void test_consumed_transaction_and_read_validation(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[2U * GDOX_LOGICAL_SECTOR_BYTES];
    uint32_t prime_count;
    uint32_t buffer_count;

    initialize_fake(&fake);
    CHECK(open_fake(&fake, 0U, &source, &error));
    CHECK(!gdox_source_read(
        &source,
        GDOX_GAMECUBE_TOTAL_SECTORS - GC_SECTORS_PER_BLOCK,
        1U,
        output,
        GDOX_LOGICAL_SECTOR_BYTES,
        &error
    ));
    CHECK(error.code == GDOX_ERROR_IO);
    CHECK(fake.last_prime_lba == 0U);
    CHECK(fake.last_displacement_lba == UINT32_C(0x03e0));
    CHECK(fake.start_count == 1U);
    prime_count = fake.prime_count;
    buffer_count = fake.buffer_count;

    CHECK(!gdox_source_read(
        &source,
        GDOX_GAMECUBE_TOTAL_SECTORS - 1U,
        2U,
        output,
        sizeof(output),
        &error
    ));
    CHECK(error.code == GDOX_ERROR_OUT_OF_BOUNDS);
    CHECK(fake.prime_count == prime_count);
    CHECK(fake.buffer_count == buffer_count);
    CHECK(!gdox_source_read(
        &source,
        0U,
        1U,
        output,
        sizeof(output),
        &error
    ));
    CHECK(error.code == GDOX_ERROR_PROTOCOL);
    CHECK(fake.prime_count == prime_count);
    CHECK(fake.buffer_count == buffer_count);
    CHECK(gdox_source_close(&source, &error));
    CHECK(!gdox_source_is_valid(&source));
    CHECK(fake.closed);
}

static void test_missing_coverage_does_not_replay(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[GDOX_LOGICAL_SECTOR_BYTES];

    initialize_fake(&fake);
    CHECK(open_fake(&fake, 0U, &source, &error));
    CHECK(!gdox_source_read(
        &source,
        16U,
        1U,
        output,
        sizeof(output),
        &error
    ));
    CHECK(error.code == GDOX_ERROR_IO);
    CHECK(strstr(error.message, "no validated coverage") != NULL);
    CHECK(gdox_source_is_valid(&source));
    CHECK(!gdox_source_read(
        &source,
        16U,
        1U,
        output,
        sizeof(output),
        &error
    ));
    CHECK(error.code == GDOX_ERROR_IO);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.start_count == 1U);
    CHECK(fake.displacement_count == 1U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(gdox_source_close(&source, &error));
}

static void test_exact_identity_is_required(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;

    initialize_fake(&fake);
    fake.wrong_identity = true;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    CHECK(!gdox_source_is_valid(&source));
    CHECK(fake.config_count == 0U);
    CHECK(fake.prime_count == 0U);
    CHECK(fake.buffer_count == 0U);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);
}

static void test_cache_validation_fails_closed(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;

    initialize_fake(&fake);
    fake.bad_frame_identity = true;
    CHECK(!open_fake(&fake, 2U, &source, &error));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(!gdox_source_is_valid(&source));
    CHECK(fake.prime_count == 1U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(fake.protocol_valid);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);

    initialize_fake(&fake);
    fake.bad_edc = true;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(strstr(error.message, "no valid Nintendo scrambling seed") != NULL);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);

    initialize_fake(&fake);
    fake.inconsistent_seed = true;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(strstr(error.message, "block failed Nintendo EDC validation") != NULL);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);

    initialize_fake(&fake);
    fake.short_buffer = true;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_TRANSPORT);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.buffer_count == 1U);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);
}

static void test_gamecube_header_is_required(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;

    initialize_fake(&fake);
    fake.bad_magic = true;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(!gdox_source_is_valid(&source));
    CHECK(fake.protocol_valid);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);
}

static void test_exact_replay_and_transport_gates(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;

    initialize_fake(&fake);
    fake.current_profile = UINT16_C(0x0010);
    fake.short_configuration = true;
    fake.configuration_failures = 2U;
    CHECK(open_fake(&fake, 0U, &source, &error));
    CHECK(gdox_source_is_valid(&source));
    CHECK(fake.config_count == 0U);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(fake.write_count == 0U);
    CHECK(gdox_source_media_present(&source));
    CHECK(fake.config_count == 0U);
    CHECK(gdox_source_close(&source, &error));

    initialize_fake(&fake);
    fake.prime_error = GDOX_ERROR_TRANSPORT;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_TRANSPORT);
    CHECK(!gdox_source_is_valid(&source));
    CHECK(fake.prime_count == 1U);
    CHECK(fake.sense_count == 0U);
    CHECK(fake.buffer_count == 0U);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);

    initialize_fake(&fake);
    fake.prime_asc = 0x04U;
    fake.prime_ascq = 0x01U;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(fake.sense[12] == 0x04U);
    CHECK(fake.sense[13] == 0x01U);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.sense_count == 0U);
    CHECK(fake.buffer_count == 0U);
    CHECK(fake.closed);

    initialize_fake(&fake);
    fake.prime_error = GDOX_ERROR_NOT_FOUND;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_NOT_FOUND);
    CHECK(!gdox_source_is_valid(&source));
    CHECK(fake.config_count == 0U);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.buffer_count == 0U);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);

    initialize_fake(&fake);
    fake.prime_sense_inline = false;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.sense_count == 0U);
    CHECK(fake.buffer_count == 0U);
    CHECK(fake.closed);

    initialize_fake(&fake);
    fake.prime_asc = 0x04U;
    fake.prime_ascq = 0x01U;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(fake.sense[12] == 0x04U);
    CHECK(fake.sense[13] == 0x01U);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.sense_count == 0U);
    CHECK(fake.buffer_count == 0U);
    CHECK(fake.closed);

    initialize_fake(&fake);
    fake.prime_error = GDOX_ERROR_NONE;
    CHECK(!open_fake(&fake, 0U, &source, &error));
    CHECK(error.code == GDOX_ERROR_PROTOCOL);
    CHECK(fake.config_count == 0U);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.buffer_count == 0U);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);
}

static void test_initial_read_does_not_retry(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;

    initialize_fake(&fake);
    fake.cache_misses = 1U;
    CHECK(!open_fake(&fake, 1U, &source, &error));
    CHECK(error.code == GDOX_ERROR_UNSUPPORTED);
    CHECK(fake.prime_count == 1U);
    CHECK(fake.sense_count == 0U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(fake.write_count == 0U);
    CHECK(fake.closed);
}

static void test_ready_timeout_adds_no_device_command(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;

    initialize_fake(&fake);
    fake.configuration_failures = 1U;
    CHECK(open_fake_with_timeout(
        &fake,
        0U,
        UINT32_C(500),
        &source,
        &error
    ));
    CHECK(fake.config_count == 0U);
    CHECK(fake.prime_count == 1U);
    CHECK(gdox_source_close(&source, &error));

    initialize_fake(&fake);
    fake.configuration_failures = 2U;
    CHECK(open_fake_with_timeout(
        &fake,
        0U,
        0U,
        &source,
        &error
    ));
    CHECK(fake.config_count == 0U);
    CHECK(fake.prime_count == 1U);
    CHECK(gdox_source_close(&source, &error));
}

static void test_terminal_full_cache_is_unconditional(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;

    initialize_fake(&fake);
    fake.target_record = 1U;
    CHECK(open_fake(&fake, 0U, &source, &error));
    CHECK(fake.prime_count == 1U);
    CHECK(fake.buffer_count == GC_METHOD4_CACHE_READS);
    CHECK(fake.protocol_valid);
    CHECK(gdox_source_close(&source, &error));
}

static void initialize_ready_fake(fake_gc *fake)
{
    initialize_fake(fake);
    fake->ready_gate_mode = true;
    fake->prime_sense_key = 3U;
    fake->prime_asc = 0x11U;
}

static void test_ready_gate_arbitrary_reads_and_restoration(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[2048];
    uint8_t expected[2048];
    initialize_ready_fake(&fake);
    CHECK(gdox_gp63_gamecube_ready_source_open(fake_opener, &fake, &source, &error));
    CHECK(fake.prime_count == 1U);
    CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
    CHECK(gdox_source_read(&source, 4112U, 1U, output, sizeof(output), &error));
    make_logical_sector(4112U, false, expected);
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
    CHECK(fake.prime_count == 2U);
    CHECK(gdox_source_read(&source, 0U, 1U, output, sizeof(output), &error));
    CHECK(fake.prime_count == 2U);
    CHECK(fake.admission_writes == 16U);
    CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
    CHECK(fake.protocol_valid && fake.write_count == 0U && fake.start_count == 0U);
    CHECK(gdox_source_close(&source, &error));
}

static void test_ready_gate_failures_restore_and_reject(void)
{
    unsigned int fault;
    for (fault = 0U; fault < 6U; ++fault) {
        fake_gc fake;
        gdox_sector_source source = {0};
        gdox_error error;
        initialize_ready_fake(&fake);
        if (fault == 0U) fake.bad_edc = true;
        if (fault == 1U) fake.bad_frame_identity = true;
        if (fault == 2U) fake.cache_misses = 2U;
        if (fault == 3U) fake.fail_admission_write = 3U;
        if (fault == 4U) fake.prime_error = GDOX_ERROR_TRANSPORT;
        if (fault == 5U) fake.short_buffer = true;
        CHECK(!gdox_gp63_gamecube_ready_source_open(fake_opener, &fake, &source, &error));
        CHECK(!gdox_source_is_valid(&source));
        CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
        CHECK(fake.closed && fake.protocol_valid && fake.write_count == 0U);
        if (fault == 0U) CHECK(fake.prime_count == 2U);
        if (fault == 4U || fault == 5U) CHECK(fake.prime_count == 1U);
    }
}

static void test_ready_gate_recovers_one_corrupt_acquisition(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[2048];
    uint8_t expected[2048];

    initialize_ready_fake(&fake);
    fake.bad_edc_acquisitions = 1U;
    CHECK(gdox_gp63_gamecube_ready_source_open(fake_opener, &fake, &source, &error));
    CHECK(fake.prime_count == 2U);
    CHECK(fake.admission_writes == 16U);
    fake.bad_edc_acquisitions = 1U;
    CHECK(gdox_source_read(&source, 4096U, 1U, output, sizeof(output), &error));
    make_logical_sector(4096U, false, expected);
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
    CHECK(fake.prime_count == 4U);
    CHECK(fake.admission_writes == 32U);
    CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
    CHECK(fake.protocol_valid);
    CHECK(gdox_source_close(&source, &error));
}

static void test_scan_source_split_records_and_bounded_recovery(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[GC_LOGICAL_BLOCK_BYTES], expected[2048];
    initialize_ready_fake(&fake);
    fake.quality_scan_mode = true;
    fake.split_records = true;
    fake.cache_misses = 1U;
    CHECK(gdox_gp63_gamecube_scan_source_open(fake_opener, &fake, &source, &error));
    CHECK(fake.prime_count == 2U);
    CHECK(gdox_source_read(&source, 494448U, 16U, output, sizeof(output), &error));
    for (uint32_t i = 0U; i < 16U; ++i) {
        make_logical_sector(494448U + i, false, expected);
        CHECK(memcmp(output + i * 2048U, expected, sizeof(expected)) == 0);
    }
    CHECK(gdox_source_read(&source, GDOX_GAMECUBE_TOTAL_SECTORS - 16U,
        16U, output, sizeof(output), &error));
    CHECK(fake.prime_count == 4U);
    fake.cache_misses = 2U;
    CHECK(gdox_source_read(&source, 8192U, 16U, output, sizeof(output), &error));
    CHECK(fake.prime_count == 7U);
    fake.cache_misses = 3U;
    CHECK(!gdox_source_read(&source, 12288U, 16U, output, sizeof(output), &error));
    CHECK(fake.prime_count == 10U);
    CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
    CHECK(fake.protocol_valid && fake.write_count == 0U);
    CHECK(gdox_source_close(&source, &error));
}

static void test_scan_failures_restore_without_replaying(void)
{
    for (unsigned int fault = 0U; fault < 4U; ++fault) {
        fake_gc fake;
        gdox_sector_source source = {0};
        gdox_error error;
        initialize_ready_fake(&fake);
        fake.quality_scan_mode = true;
        fake.short_counters = fault == 0U;
        fake.fail_seek = fault == 1U;
        if (fault == 2U) fake.prime_error = GDOX_ERROR_TRANSPORT;
        if (fault == 3U) fake.fail_admission_write = 5U;
        CHECK(!gdox_gp63_gamecube_scan_source_open(fake_opener, &fake, &source, &error));
        CHECK(fake.prime_count <= 1U);
        if (gdox_source_is_valid(&source)) {
            CHECK(!fake.closed);
            CHECK(gdox_source_close(&source, &error));
        }
        CHECK(!gdox_source_is_valid(&source));
        if (fault != 3U) CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
        CHECK(fake.closed && fake.protocol_valid);
    }
}

static void test_outer_parity_recovers_data_and_rejects_excess_errors(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[GC_LOGICAL_BLOCK_BYTES], expected[2048];
    initialize_ready_fake(&fake);
    fake.quality_scan_mode = true;
    fake.outer_parity = true;
    fake.split_records = true;
    fake.bad_frame_identity = true;
    CHECK(gdox_gp63_gamecube_scan_source_open(fake_opener, &fake, &source, &error));
    CHECK(gdox_source_read(&source, 625600U, 16U, output, sizeof(output), &error));
    for (uint32_t i = 0U; i < 16U; ++i) {
        make_logical_sector(625600U + i, false, expected);
        CHECK(memcmp(output + i * 2048U, expected, sizeof(expected)) == 0);
    }
    CHECK(fake.prime_count == 2U);
    fake.uncorrectable_parity = true;
    CHECK(!gdox_source_read(&source, 8192U, 16U, output, sizeof(output), &error));
    CHECK(fake.prime_count == 5U);
    CHECK(error.code == GDOX_ERROR_INVALID_SOURCE);
    CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
    CHECK(fake.protocol_valid);
    CHECK(gdox_source_close(&source, &error));
}

static void test_outer_parity_multiple_errors(void)
{
    for (unsigned pattern = 0U; pattern < 3U; ++pattern) {
        fake_gc fake;
        gdox_sector_source source = {0};
        gdox_error error;
        uint8_t output[GC_LOGICAL_BLOCK_BYTES], expected[2048];
        initialize_ready_fake(&fake);
        fake.quality_scan_mode = true;
        fake.split_records = true;
        fake.parity_pattern = pattern;
        CHECK(gdox_gp63_gamecube_scan_source_open(fake_opener, &fake, &source, &error));
        for (unsigned errors = 1U; errors <= 9U; ++errors) {
            const uint32_t lba = errors * 4096U;
            fake.parity_errors = errors;
            const bool ok = gdox_source_read(&source, lba, 16U, output, sizeof(output), &error);
            if (errors <= 8U) {
                CHECK(ok);
                for (uint32_t i = 0U; i < 16U; ++i) {
                    make_logical_sector(lba + i, false, expected);
                    CHECK(memcmp(output + i * 2048U, expected, sizeof(expected)) == 0);
                }
            } else {
                CHECK(!ok && error.code == GDOX_ERROR_INVALID_SOURCE);
            }
            CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
        }
        CHECK(fake.protocol_valid);
        CHECK(gdox_source_close(&source, &error));
    }
}

static void test_ready_gate_restore_failure_poisoning(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[2048];
    uint32_t primes;
    initialize_ready_fake(&fake);
    CHECK(gdox_gp63_gamecube_ready_source_open(fake_opener, &fake, &source, &error));
    fake.fail_admission_write = fake.admission_writes + 5U;
    CHECK(!gdox_source_read(&source, 4096U, 1U, output, sizeof(output), &error));
    CHECK(!gdox_source_media_present(&source));
    primes = fake.prime_count;
    CHECK(!gdox_source_read(&source, 8192U, 1U, output, sizeof(output), &error));
    CHECK(fake.prime_count == primes);
    CHECK(gdox_source_close(&source, &error));
    CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
}

static void test_failed_open_retains_restoration_ownership(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    initialize_ready_fake(&fake);
    fake.fail_restoration = true;
    CHECK(!gdox_gp63_gamecube_ready_source_open(fake_opener, &fake, &source, &error));
    CHECK(gdox_source_is_valid(&source) && !fake.closed);
    CHECK(!gdox_source_close(&source, &error));
    CHECK(gdox_source_is_valid(&source) && !fake.closed);
    fake.fail_restoration = false;
    CHECK(gdox_source_close(&source, &error));
    CHECK(!gdox_source_is_valid(&source) && fake.closed && fake.protocol_valid);
    CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
}

static void test_close_requires_restoration_or_confirmed_disconnect(void)
{
    for (unsigned outcome = 0U; outcome < 2U; ++outcome) {
        fake_gc fake;
        gdox_sector_source source = {0};
        gdox_error error;
        uint8_t output[2048];
        initialize_ready_fake(&fake);
        CHECK(gdox_gp63_gamecube_ready_source_open(fake_opener, &fake, &source, &error));
        fake.fail_restoration = true;
        CHECK(!gdox_source_read(&source, 4096U, 1U, output, sizeof(output), &error));
        fake.presence_unknown = true;
        CHECK(!gdox_source_close(&source, &error));
        CHECK(gdox_source_is_valid(&source) && !fake.closed);
        fake.presence_unknown = false;
        if (outcome == 0U) {
            fake.fail_restoration = false;
        } else {
            fake.disconnected = true;
        }
        const uint32_t writes = fake.admission_writes;
        CHECK(gdox_source_close(&source, &error));
        CHECK(!gdox_source_is_valid(&source) && fake.closed && fake.protocol_valid);
        if (outcome == 0U) {
            CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
        } else {
            CHECK(fake.admission_writes == writes);
        }
    }
}

static void test_ram_buffer_lru_and_bounded_eviction(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[2048], expected[2048];
    initialize_ready_fake(&fake);
    CHECK(gdox_gp63_gamecube_ready_source_open(fake_opener, &fake, &source, &error));
    CHECK(gdox_source_read(&source, 1024U, 1U, output, sizeof(output), &error));
    CHECK(gdox_source_read(&source, 0U, 1U, output, sizeof(output), &error));
    CHECK(fake.prime_count == 2U); /* Old direct-mapped collision is gone. */
    for (uint32_t block = 1U; block <= 512U; ++block) {
        CHECK(gdox_source_read(&source, block * 16U, 1U, output, sizeof(output), &error));
    }
    const uint32_t primes = fake.prime_count;
    CHECK(gdox_source_read(&source, 0U, 1U, output, sizeof(output), &error));
    make_logical_sector(0U, false, expected);
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
    CHECK(fake.prime_count == primes + 1U); /* Capacity stays bounded. */
    CHECK(gdox_source_close(&source, &error));
}

static void *release_paused_scan(void *raw)
{
    fake_gc *fake = raw;
    gdox_sleep_ms(250U);
    atomic_store(&fake->scan_release, true);
    return NULL;
}

static void test_readahead_concurrency_fault_and_recovery_limits(void)
{
    for (unsigned outcome = 0U; outcome < 3U; ++outcome) {
        fake_gc fake;
        gdox_sector_source source = {0};
        gdox_error error;
        uint8_t output[32768], expected[2048];
        initialize_ready_fake(&fake);
        fake.quality_scan_mode = true;
        fake.pause_scan = true;
        fake.pause_outcome = outcome;
        CHECK(gdox_gp63_gamecube_stream_source_open(fake_opener, &fake, &source, &error));
        CHECK(gdox_source_read(&source, 0U, 16U, output, sizeof(output), &error));
        CHECK(gdox_source_read(&source, 16U, 16U, output, sizeof(output), &error));
        const uint64_t deadline = gdox_monotonic_ms() + 2000U;
        while (!atomic_load(&fake.scan_entered) && gdox_monotonic_ms() < deadline) gdox_sleep_ms(1U);
        const bool entered = atomic_load(&fake.scan_entered);
        if (!entered) {
            gdox_source_abort(&source); atomic_store(&fake.scan_release, true);
            gdox_source_close(&source, &error); CHECK(entered);
        }
        pthread_t release;
        const int started = pthread_create(&release, NULL, release_paused_scan, &fake);
        if (started != 0) {
            atomic_store(&fake.scan_release, true); gdox_source_close(&source, &error); CHECK(started == 0);
        }
        const uint64_t start = gdox_monotonic_ms();
        const bool buffered = gdox_source_read(&source, 0U, 1U, output, 2048U, &error);
        const uint64_t elapsed = gdox_monotonic_ms() - start;
        make_logical_sector(0U, false, expected);
        const bool correct = memcmp(output, expected, sizeof(expected)) == 0;
        if (outcome == 0U) gdox_source_abort(&source);
        pthread_join(release, NULL);
        bool fault_rejected = true;
        if (outcome == 1U) {
            const uint64_t until = gdox_monotonic_ms() + 1000U;
            while (gdox_source_media_present(&source) && gdox_monotonic_ms() < until) gdox_sleep_ms(1U);
            fault_rejected = !gdox_source_read(&source, 0U, 1U, output, 2048U, &error)
                && error.code == GDOX_ERROR_TRANSPORT;
        } else if (outcome == 2U) {
            const uint64_t until = gdox_monotonic_ms() + 1000U;
            while (!atomic_load(&fake.capture_finished) && gdox_monotonic_ms() < until) gdox_sleep_ms(1U);
            gdox_sleep_ms(20U);
        } else {
            fault_rejected = !gdox_source_read(&source, 0U, 1U, output, 2048U, &error)
                && error.code == GDOX_ERROR_CANCELLED;
        }
        const bool closed = gdox_source_close(&source, &error);
        CHECK(buffered && correct && elapsed < 100U);
        CHECK(fault_rejected && closed && fake.closed && fake.protocol_valid);
        CHECK(fake.prime_count == 3U && fake.normal_reads == 0U);
        CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
    }
}

static void test_readahead_follows_short_movie_fragments(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[32768];
    initialize_ready_fake(&fake);
    fake.quality_scan_mode = true;
    fake.pause_scan = true;
    CHECK(gdox_gp63_gamecube_stream_source_open(fake_opener, &fake, &source, &error));
    /* Every large read is separated by short packet tails. Ignoring the
     * tails prevents speculation entirely despite a contiguous stream. */
    const uint32_t starts[] = {0U, 8U, 12U, 16U, 24U, 28U};
    const uint32_t lengths[] = {8U, 4U, 4U, 8U, 4U, 4U};
    bool read_ok = true;
    for (size_t i = 0U; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        if (!gdox_source_read(&source, starts[i], lengths[i], output,
                (size_t)lengths[i] * 2048U, &error)) {
            read_ok = false;
            break;
        }
    }
    const uint64_t deadline = gdox_monotonic_ms() + 2000U;
    while (!atomic_load(&fake.scan_entered) && gdox_monotonic_ms() < deadline) gdox_sleep_ms(1U);
    const bool prefetched = atomic_load(&fake.scan_entered);
    gdox_source_abort(&source);
    atomic_store(&fake.scan_release, true);
    const bool closed = gdox_source_close(&source, &error);
    CHECK(read_ok && prefetched && closed);
    CHECK(fake.prime_count == 3U && fake.normal_reads == 0U);
    CHECK(fake.protocol_valid && memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
}

static void test_readahead_resumes_after_rejected_block(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[32768];
    initialize_ready_fake(&fake);
    fake.quality_scan_mode = true;
    fake.pause_scan = true;
    fake.pause_outcome = 2U;
    atomic_store(&fake.scan_release, true);
    CHECK(gdox_gp63_gamecube_stream_source_open(fake_opener, &fake, &source, &error));
    CHECK(gdox_source_read(&source, 0U, 16U, output, sizeof(output), &error));
    CHECK(gdox_source_read(&source, 16U, 16U, output, sizeof(output), &error));
    const uint64_t deadline = gdox_monotonic_ms() + 2000U;
    while (!atomic_load(&fake.capture_finished) && gdox_monotonic_ms() < deadline) gdox_sleep_ms(1U);
    if (!atomic_load(&fake.capture_finished)) {
        gdox_source_abort(&source); gdox_source_close(&source, &error); CHECK(false);
    }
    /* The rejected capture is complete; its transport callback no longer
     * reads these fixture controls. Pause the immediately following block. */
    fake.pause_lba = 48U;
    fake.pause_outcome = 0U;
    atomic_store(&fake.scan_entered, false);
    atomic_store(&fake.scan_release, false);
    const bool demand_ok = gdox_source_read(&source, 32U, 16U, output, sizeof(output), &error);
    const uint64_t until = gdox_monotonic_ms() + 2000U;
    while (!atomic_load(&fake.scan_entered) && gdox_monotonic_ms() < until) gdox_sleep_ms(1U);
    const bool resumed = atomic_load(&fake.scan_entered);
    gdox_source_abort(&source);
    atomic_store(&fake.scan_release, true);
    const bool closed = gdox_source_close(&source, &error);
    CHECK(demand_ok && resumed && closed);
    CHECK(fake.prime_count == 5U && fake.normal_reads == 0U);
    CHECK(fake.protocol_valid && memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
}

static void test_cancel_rejects_buffered_data(void)
{
    fake_gc fake;
    gdox_sector_source source = {0};
    gdox_error error;
    uint8_t output[2048];
    initialize_ready_fake(&fake);
    CHECK(gdox_gp63_gamecube_ready_source_open(fake_opener, &fake, &source, &error));
    gdox_source_abort(&source);
    CHECK(!gdox_source_read(&source, 0U, 1U, output, sizeof(output), &error));
    CHECK(error.code == GDOX_ERROR_CANCELLED);
    CHECK(fake.prime_count == 1U);
    CHECK(gdox_source_close(&source, &error));
}

static void test_wii_disc_and_bounds(void)
{
    for (unsigned fault = 0U; fault <= 8U; ++fault) {
        fake_gc fake;
        gdox_sector_source source = {0};
        gdox_error error;
        uint8_t output[GC_LOGICAL_BLOCK_BYTES];
        uint8_t expected[GDOX_LOGICAL_SECTOR_BYTES];
        initialize_fake(&fake);
        fake.wii = true;
        fake.wii_fault = fault;
        fake.ready_gate_mode = true;
        fake.quality_scan_mode = true;
        const bool opened = gdox_gp63_gamecube_scan_source_open(fake_opener, &fake, &source, &error);
        if (fault != 0U) {
            CHECK(!opened);
            CHECK(error.code == (fault == 5U ? GDOX_ERROR_UNSUPPORTED : GDOX_ERROR_INVALID_VOLUME));
            CHECK(!gdox_source_is_valid(&source));
            CHECK(fake.closed);
        } else {
            CHECK(opened);
            CHECK(gdox_gp63_gamecube_source_profile(&source) == GDOX_OPTICAL_MEDIA_WII);
            CHECK(gdox_source_sector_count(&source) == GDOX_WII_SL_TOTAL_SECTORS);
            const uint32_t sectors[] = {712880U, 1048576U, (uint32_t)GDOX_WII_SL_TOTAL_SECTORS - 16U};
            for (size_t i = 0U; i < sizeof(sectors)/sizeof(sectors[0]); ++i) {
                CHECK(gdox_source_read(&source, sectors[i], 16U, output, sizeof(output), &error));
                for (uint32_t j = 0U; j < 16U; ++j) {
                    make_logical_sector(sectors[i] + j, false, expected);
                    CHECK(memcmp(output + j * GDOX_LOGICAL_SECTOR_BYTES, expected, sizeof(expected)) == 0);
                }
            }
            CHECK(!gdox_source_read(&source, GDOX_WII_SL_TOTAL_SECTORS, 1U,
                output, GDOX_LOGICAL_SECTOR_BYTES, &error));
            CHECK(error.code == GDOX_ERROR_OUT_OF_BOUNDS);
            CHECK(gdox_source_close(&source, &error));
        }
        CHECK(fake.protocol_valid);
        CHECK(memcmp(fake.admission, (uint8_t[4]){0}, 4U) == 0);
    }
}

int main(void)
{
    test_close_requires_restoration_or_confirmed_disconnect();
    test_outer_parity_recovers_data_and_rejects_excess_errors();
    test_outer_parity_multiple_errors();
    test_readahead_follows_short_movie_fragments();
    test_readahead_resumes_after_rejected_block();
    test_scan_source_split_records_and_bounded_recovery();
    test_scan_failures_restore_without_replaying();
    test_cancel_rejects_buffered_data();
    test_ram_buffer_lru_and_bounded_eviction();
    test_readahead_concurrency_fault_and_recovery_limits();
    test_ready_gate_arbitrary_reads_and_restoration();
    test_ready_gate_failures_restore_and_reject();
    test_ready_gate_recovers_one_corrupt_acquisition();
    test_ready_gate_restore_failure_poisoning();
    test_failed_open_retains_restoration_ownership();
    test_successful_read_only_source();
    test_consumed_transaction_and_read_validation();
    test_missing_coverage_does_not_replay();
    test_exact_identity_is_required();
    test_cache_validation_fails_closed();
    test_gamecube_header_is_required();
    test_wii_disc_and_bounds();
    test_exact_replay_and_transport_gates();
    test_initial_read_does_not_retry();
    test_ready_timeout_adds_no_device_command();
    test_terminal_full_cache_is_unconditional();
    if (failures != 0) {
        (void)fprintf(stderr, "%d GameCube source check(s) failed\n", failures);
        return 1;
    }
    (void)puts("GDOX GP63 GameCube source tests passed");
    return 0;
}
