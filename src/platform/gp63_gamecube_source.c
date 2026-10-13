#define _POSIX_C_SOURCE 200809L

#include "gdox/optical.h"

#include "platform/gp63_gamecube_source.h"
#include "platform/portable_sync.h"
#include "platform/scsi_transport.h"
#if defined(GDOX_HAS_LINUX_SG) && GDOX_HAS_LINUX_SG
#include "platform/gp63_sg_linux.h"
#endif

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GDOX_GC_INQUIRY_TIMEOUT_MS UINT32_C(10000)
#define GDOX_GC_READ_TIMEOUT_MS UINT32_C(30000)
#define GDOX_GC_SECTORS_PER_BLOCK UINT32_C(16)
#define GDOX_GC_CACHE_RECORD_BYTES 2236U
#define GDOX_GC_FRAME_BYTES 2064U
#define GDOX_GC_RAW_BLOCK_BYTES \
    (GDOX_GC_CACHE_RECORD_BYTES * GDOX_GC_SECTORS_PER_BLOCK)
#define GDOX_GC_LOGICAL_BLOCK_BYTES \
    (GDOX_LOGICAL_SECTOR_BYTES * GDOX_GC_SECTORS_PER_BLOCK)
#define GDOX_GC_CACHE_SCAN_BYTES UINT32_C(0x0c0000)
#define GDOX_GC_CACHE_CHUNK_BYTES UINT32_C(0x010000)
#define GDOX_GC_PHYSICAL_BASE UINT32_C(0x30000)
#define GDOX_GC_METHOD4_DISPLACEMENT_SECTORS UINT32_C(0x03e0)
#define GDOX_GC_METHOD4_DISPLACEMENT_BYTES UINT32_C(65536)
#define GDOX_GC_METHOD4_STREAM_SECTORS UINT32_C(0x1b)
#define GDOX_GC_METHOD4_STREAM_BYTES \
    (GDOX_GC_FRAME_BYTES * GDOX_GC_METHOD4_STREAM_SECTORS)
#define GDOX_GC_METHOD4_CACHE_BYTES GDOX_GC_METHOD4_STREAM_BYTES
#define GDOX_GC_METHOD4_TIMEOUT_MS UINT32_C(100000)
#define GDOX_GC_SEED_BITS 15U
#define GDOX_GC_EDC_BITS 32U
#define GDOX_GC_MAGIC UINT32_C(0xc2339f3d)
#define GDOX_GC_BUFFER_BLOCKS 512U
#define GDOX_GC_STREAMS 4U
#define GDOX_GC_READAHEAD_SECTORS 1024U

typedef struct gdox_gc_stream {
    uint32_t last_end;
    uint32_t next;
    uint32_t end;
    uint32_t retry_after;
    uint64_t generation;
    uint64_t used;
    bool valid;
} gdox_gc_stream;

typedef struct gdox_gc_identity {
    char vendor[9];
    char model[17];
    char revision[5];
} gdox_gc_identity;

typedef struct gdox_gc_context {
    gdox_scsi_transport transport;
    gdox_mutex mutex;
    gdox_mutex cache_mutex;
    pthread_cond_t work_ready;
    pthread_t worker;
    bool worker_started;
    atomic_uint demand_reads;
    atomic_bool abort;
    uint32_t edc_table[256];
    uint8_t decode_buffer[GDOX_GC_LOGICAL_BLOCK_BYTES];
    uint8_t producer_buffer[GDOX_GC_METHOD4_DISPLACEMENT_BYTES];
    uint8_t *decoded_cache;
    uint32_t decoded_tags[GDOX_GC_BUFFER_BLOCKS];
    uint64_t decoded_used[GDOX_GC_BUFFER_BLOCKS];
    uint64_t use_counter;
    gdox_gc_stream streams[GDOX_GC_STREAMS];
    uint8_t *raw_cache;
    bool producer_started;
    bool raw_cache_valid;
    atomic_bool session_present;
    bool ready_gate;
    bool quality_scan;
    atomic_bool poisoned;
    uint8_t restore_pending;
    size_t raw_cache_bytes;
} gdox_gc_context;

static const uint16_t admission_addresses[4] = {
    0x8538U, 0x8539U, 0x853aU, 0x037eU,
};

typedef enum gdox_gc_seed_solution {
    GDOX_GC_SEED_SOLUTION_UNIQUE,
    GDOX_GC_SEED_SOLUTION_INCONSISTENT,
    GDOX_GC_SEED_SOLUTION_AMBIGUOUS
} gdox_gc_seed_solution;

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

static void copy_ascii_field(
    char *output,
    size_t output_bytes,
    const uint8_t *input,
    size_t input_bytes
)
{
    size_t begin = 0U;
    size_t end = input_bytes;
    size_t length;

    while (begin < end && (input[begin] == ' ' || input[begin] == 0U)) {
        ++begin;
    }
    while (end > begin && (input[end - 1U] == ' ' || input[end - 1U] == 0U)) {
        --end;
    }
    length = end - begin;
    if (length >= output_bytes) {
        length = output_bytes - 1U;
    }
    memcpy(output, input + begin, length);
    output[length] = '\0';
}

static bool inquiry(
    gdox_scsi_transport *transport,
    gdox_gc_identity *identity,
    gdox_error *error
)
{
    uint8_t cdb[6] = {0x12U, 0U, 0U, 0U, 36U, 0U};
    uint8_t response[36];
    size_t transferred;

    if (!gdox_scsi_command_in(
            transport,
            "INQUIRY",
            cdb,
            sizeof(cdb),
            response,
            sizeof(response),
            GDOX_GC_INQUIRY_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred < 36U) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "INQUIRY returned fewer than 36 bytes");
        return false;
    }
    copy_ascii_field(identity->vendor, sizeof(identity->vendor), response + 8U, 8U);
    copy_ascii_field(identity->model, sizeof(identity->model), response + 16U, 16U);
    copy_ascii_field(identity->revision, sizeof(identity->revision), response + 32U, 4U);
    return true;
}

static bool identity_is_gp63(const gdox_gc_identity *identity)
{
    return strcmp(identity->vendor, "HL-DT-ST") == 0
        && strcmp(identity->model, "DVDRAM GP63EX70") == 0
        && strcmp(identity->revision, "RF02") == 0;
}

static uint32_t edc_table_value(uint8_t index)
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

static uint32_t edc_update(
    const uint32_t table[256],
    uint32_t edc,
    uint8_t value
)
{
    return table[(uint8_t)((edc >> 24U) ^ value)]
        ^ (edc << 8U);
}

static void initialize_edc_table(uint32_t table[256])
{
    unsigned int index;
    for (index = 0U; index < 256U; ++index) {
        table[index] = edc_table_value((uint8_t)index);
    }
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

static bool frame_identity_matches(const uint8_t *frame, uint32_t lba)
{
    return (frame[0] & 1U) == 0U
        && read_be_u24(frame + 1U) == GDOX_GC_PHYSICAL_BASE + lba;
}

static uint32_t decoded_frame_edc(
    const uint32_t table[256],
    const uint8_t *frame,
    uint16_t seed,
    uint8_t *output
)
{
    uint16_t lfsr = seed;
    uint32_t edc = 0U;
    size_t index;

    for (index = 0U; index < 12U; ++index) {
        edc = edc_update(table, edc, frame[index]);
    }
    if (output != NULL) {
        memcpy(output, frame + 6U, 6U);
    }
    for (index = 0U; index < GDOX_LOGICAL_SECTOR_BYTES; ++index) {
        const uint8_t decoded =
            (uint8_t)(frame[12U + index] ^ lfsr_byte(&lfsr));
        edc = edc_update(table, edc, decoded);
        if (output != NULL && index < GDOX_LOGICAL_SECTOR_BYTES - 6U) {
            output[6U + index] = decoded;
        }
    }
    return edc;
}

static bool decode_frame(
    const uint32_t table[256],
    const uint8_t *frame,
    uint16_t seed,
    uint8_t *output
)
{
    return decoded_frame_edc(table, frame, seed, output)
        == read_be_u32(frame + 2060U);
}

static gdox_gc_seed_solution solve_frame_seed(
    const uint32_t table[256],
    const uint8_t *frame,
    uint16_t *seed
)
{
    const uint32_t coefficient_mask =
        (UINT32_C(1) << GDOX_GC_SEED_BITS) - UINT32_C(1);
    const uint32_t right_hand_side =
        UINT32_C(1) << GDOX_GC_SEED_BITS;
    uint32_t columns[GDOX_GC_SEED_BITS];
    uint32_t equations[GDOX_GC_EDC_BITS];
    uint8_t pivot_columns[GDOX_GC_SEED_BITS];
    const uint32_t baseline = decoded_frame_edc(table, frame, 0U, NULL);
    const uint32_t target = read_be_u32(frame + 2060U) ^ baseline;
    unsigned int variable;
    unsigned int output_bit;
    unsigned int rank = 0U;
    uint16_t result = 0U;

    /*
     * The LFSR and EDC are linear over GF(2). Each column is the EDC
     * contribution of one seed bit relative to the zero-seed baseline.
     */
    for (variable = 0U; variable < GDOX_GC_SEED_BITS; ++variable) {
        columns[variable] = decoded_frame_edc(
            table,
            frame,
            (uint16_t)(UINT16_C(1) << variable),
            NULL
        ) ^ baseline;
    }
    for (output_bit = 0U; output_bit < GDOX_GC_EDC_BITS; ++output_bit) {
        uint32_t equation =
            ((target >> output_bit) & UINT32_C(1))
                << GDOX_GC_SEED_BITS;

        for (variable = 0U; variable < GDOX_GC_SEED_BITS; ++variable) {
            equation |=
                ((columns[variable] >> output_bit) & UINT32_C(1))
                    << variable;
        }
        equations[output_bit] = equation;
    }
    for (variable = 0U; variable < GDOX_GC_SEED_BITS; ++variable) {
        unsigned int pivot;
        unsigned int row;

        for (pivot = rank; pivot < GDOX_GC_EDC_BITS; ++pivot) {
            if ((equations[pivot] & (UINT32_C(1) << variable)) != 0U) {
                break;
            }
        }
        if (pivot == GDOX_GC_EDC_BITS) {
            continue;
        }
        if (pivot != rank) {
            const uint32_t temporary = equations[rank];
            equations[rank] = equations[pivot];
            equations[pivot] = temporary;
        }
        pivot_columns[rank] = (uint8_t)variable;
        for (row = 0U; row < GDOX_GC_EDC_BITS; ++row) {
            if (row != rank
                && (equations[row] & (UINT32_C(1) << variable)) != 0U) {
                equations[row] ^= equations[rank];
            }
        }
        ++rank;
    }
    for (output_bit = 0U; output_bit < GDOX_GC_EDC_BITS; ++output_bit) {
        if ((equations[output_bit] & coefficient_mask) == 0U
            && (equations[output_bit] & right_hand_side) != 0U) {
            return GDOX_GC_SEED_SOLUTION_INCONSISTENT;
        }
    }
    if (rank != GDOX_GC_SEED_BITS) {
        return GDOX_GC_SEED_SOLUTION_AMBIGUOUS;
    }
    for (variable = 0U; variable < rank; ++variable) {
        if ((equations[variable] & right_hand_side) != 0U) {
            result |= (uint16_t)(
                UINT16_C(1) << pivot_columns[variable]
            );
        }
    }
    *seed = result;
    return GDOX_GC_SEED_SOLUTION_UNIQUE;
}

static bool seed_decodes_block(
    const uint32_t table[256],
    const uint8_t raw[GDOX_GC_RAW_BLOCK_BYTES],
    uint32_t lba,
    uint16_t seed,
    uint8_t *output
)
{
    uint32_t index;

    for (index = 0U; index < GDOX_GC_SECTORS_PER_BLOCK; ++index) {
        const uint8_t *frame =
            raw + (size_t)index * GDOX_GC_CACHE_RECORD_BYTES;
        uint8_t *sector = output == NULL
            ? NULL
            : output + (size_t)index * GDOX_LOGICAL_SECTOR_BYTES;

        if (!frame_identity_matches(frame, lba + index)
            || !decode_frame(table, frame, seed, sector)) {
            return false;
        }
    }
    return true;
}

static bool decode_block(
    gdox_gc_context *context,
    const uint8_t raw[GDOX_GC_RAW_BLOCK_BYTES],
    uint32_t lba,
    gdox_error *error
)
{
    gdox_gc_seed_solution solution;
    uint32_t index;
    uint16_t seed;

    for (index = 0U; index < GDOX_GC_SECTORS_PER_BLOCK; ++index) {
        const uint8_t *frame =
            raw + (size_t)index * GDOX_GC_CACHE_RECORD_BYTES;
        if (!frame_identity_matches(frame, lba + index)) {
            gdox_error_set(
                error,
                GDOX_ERROR_INVALID_SOURCE,
                "GP63 cache did not contain the requested GameCube sector sequence"
            );
            return false;
        }
    }
    solution = solve_frame_seed(context->edc_table, raw, &seed);
    if (solution != GDOX_GC_SEED_SOLUTION_UNIQUE) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            solution == GDOX_GC_SEED_SOLUTION_INCONSISTENT
                ? "GP63 cache frame has no valid GameCube scrambling seed"
                : "GP63 cache frame has an ambiguous GameCube scrambling seed"
        );
        return false;
    }
    if (!seed_decodes_block(
            context->edc_table,
            raw,
            lba,
            seed,
            context->decode_buffer
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "GP63 cache block failed GameCube EDC validation"
        );
        return false;
    }
    return true;
}

/* Fixed eight-MiB RAM buffer with LRU replacement. The RAM lock is never
 * held during drive I/O, so buffered reads can finish while read-ahead runs. */
static size_t decoded_slot_locked(const gdox_gc_context *context, size_t block_index)
{
    for (size_t i = 0U; i < GDOX_GC_BUFFER_BLOCKS; ++i) {
        if (context->decoded_tags[i] == block_index + 1U) return i;
    }
    return GDOX_GC_BUFFER_BLOCKS;
}

static bool decoded_block_is_valid(gdox_gc_context *context, size_t block_index)
{
    gdox_mutex_lock(&context->cache_mutex);
    const bool found = decoded_slot_locked(context, block_index) < GDOX_GC_BUFFER_BLOCKS;
    gdox_mutex_unlock(&context->cache_mutex);
    return found;
}

static bool retain_decoded_block(gdox_gc_context *context, size_t block_index, gdox_error *error)
{
    (void)error;
    gdox_mutex_lock(&context->cache_mutex);
    size_t slot = decoded_slot_locked(context, block_index);
    if (slot == GDOX_GC_BUFFER_BLOCKS) {
        slot = 0U;
        for (size_t i = 0U; i < GDOX_GC_BUFFER_BLOCKS; ++i) {
            if (context->decoded_tags[i] == 0U) { slot = i; break; }
            if (context->decoded_used[i] < context->decoded_used[slot]) slot = i;
        }
    }
    memcpy(context->decoded_cache + slot * GDOX_GC_LOGICAL_BLOCK_BYTES,
        context->decode_buffer, GDOX_GC_LOGICAL_BLOCK_BYTES);
    context->decoded_tags[slot] = (uint32_t)block_index + 1U;
    context->decoded_used[slot] = ++context->use_counter;
    gdox_mutex_unlock(&context->cache_mutex);
    return true;
}

static bool copy_decoded(gdox_gc_context *context, size_t block_index,
    size_t offset, size_t bytes, uint8_t *output)
{
    gdox_mutex_lock(&context->cache_mutex);
    const size_t slot = decoded_slot_locked(context, block_index);
    if (slot < GDOX_GC_BUFFER_BLOCKS) {
        memcpy(output, context->decoded_cache + slot * GDOX_GC_LOGICAL_BLOCK_BYTES + offset, bytes);
        context->decoded_used[slot] = ++context->use_counter;
    }
    gdox_mutex_unlock(&context->cache_mutex);
    return slot < GDOX_GC_BUFFER_BLOCKS;
}

static bool load_decoded_block(gdox_gc_context *context, size_t block_index, gdox_error *error)
{
    if (copy_decoded(context, block_index, 0U, GDOX_GC_LOGICAL_BLOCK_BYTES,
            context->decode_buffer)) return true;
    gdox_error_set(error, GDOX_ERROR_INTERNAL, "GameCube RAM buffer does not cover the requested block");
    return false;
}

static bool last_sense_matches(gdox_gc_context *context, uint8_t key, uint8_t asc)
{
    uint8_t sense[64];
    size_t count = 0U;
    return gdox_scsi_transport_last_sense(&context->transport, sense, sizeof(sense), &count)
        && count == 18U && (sense[0] == 0x70U || sense[0] == 0x71U)
        && sense[2] == key && sense[12] == asc && sense[13] == 0U;
}

static bool accept_cache_command_completion(
    gdox_gc_context *context,
    gdox_error *completion,
    gdox_error *error
)
{
    if (completion->code != GDOX_ERROR_IO || !last_sense_matches(context, 2U, 0x30U)) {
        *error = *completion;
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static bool read_raw_cache(
    gdox_gc_context *context,
    uint32_t address,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
);

static bool read_method4_cache(
    gdox_gc_context *context,
    gdox_error *error
)
{
    static const uint8_t cdb[12] = {
        0x3cU, 0x02U, 0U, 0U, 0U, 0U,
        0U, 0xd9U, 0xb0U, 0U, 0U, 0U,
    };
    size_t transferred;

    if (!gdox_scsi_command_in(
            &context->transport,
            "READ BUFFER (GameCube method-4 cache)",
            cdb,
            sizeof(cdb),
            context->raw_cache,
            GDOX_GC_METHOD4_CACHE_BYTES,
            GDOX_GC_READ_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != GDOX_GC_METHOD4_CACHE_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_TRANSPORT,
            "GameCube method-4 cache returned a short transfer"
        );
        return false;
    }
    context->raw_cache_bytes = GDOX_GC_METHOD4_CACHE_BYTES;
    context->raw_cache_valid = true;
    return true;
}

static bool read_admission_byte(
    gdox_gc_context *context,
    uint16_t address,
    uint8_t *value,
    gdox_error *error
)
{
    uint8_t cdb[10] = {0xf1U, 2U, 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U};
    uint8_t data[4];
    size_t transferred = 0U;

    cdb[4] = (uint8_t)(address >> 8U);
    cdb[5] = (uint8_t)address;
    if (!gdox_scsi_command_in(
            &context->transport, "RF02 admission-state read", cdb, sizeof(cdb),
            data, sizeof(data), GDOX_GC_INQUIRY_TIMEOUT_MS, &transferred, error)) {
        return false;
    }
    if (transferred != sizeof(data) || data[0] != 0U
        || data[1] != 0U || data[2] != 0U) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "RF02 admission-state read was malformed");
        return false;
    }
    *value = data[3];
    return true;
}

static bool write_admission_byte(
    gdox_gc_context *context,
    uint16_t address,
    uint8_t value,
    gdox_error *error
)
{
    uint8_t cdb[10] = {0xf1U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    uint8_t observed = 0U;

    if (address != UINT16_C(0x037e)
        && (address < UINT16_C(0x8538) || address > UINT16_C(0x853a))) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "invalid RF02 admission field");
        return false;
    }
    cdb[4] = (uint8_t)(address >> 8U);
    cdb[5] = (uint8_t)address;
    cdb[9] = value;
    if (!gdox_scsi_command_none(
            &context->transport, "RF02 volatile admission-state write", cdb, sizeof(cdb),
            GDOX_GC_INQUIRY_TIMEOUT_MS, error)
        || !read_admission_byte(context, address, &observed, error)) {
        return false;
    }
    if (observed != value) {
        gdox_error_set(error, GDOX_ERROR_PROTOCOL, "RF02 admission-state write did not verify");
        return false;
    }
    return true;
}

static bool restore_admission(gdox_gc_context *context, gdox_error *error)
{
    gdox_error failure;
    bool success = true;
    bool present = true;

    if (context->restore_pending == 0U) return true;
    if (gdox_scsi_transport_device_present(&context->transport, &present, &failure)
        && !present) {
        /* The transport reports loss of this exact open device, not a timeout. */
        context->restore_pending = 0U;
        return true;
    }
    for (size_t count = 4U; count != 0U; --count) {
        const size_t index = count - 1U;
        const uint8_t bit = (uint8_t)(1U << index);
        if ((context->restore_pending & bit) == 0U) continue;
        if (write_admission_byte(context, admission_addresses[index], 0U, &failure)) {
            context->restore_pending &= (uint8_t)~bit;
        } else {
            if (success) *error = failure;
            success = false;
        }
    }
    return success;
}

static bool run_ready_cache_producer(
    gdox_gc_context *context,
    uint32_t lba,
    bool quality_scan,
    gdox_error *error
)
{
    /* L259: exact RF02 volatile ready/physical-limit gates; no flash writes. */
    const uint16_t *addresses = admission_addresses;
    static const uint8_t tur[6] = {0};
    uint8_t cdb[12] = {0xa8U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 0x80U, 0U};
    uint8_t saved[4];
    uint8_t desired[4];
    size_t transferred = 0U;
    size_t index;
    bool success = false;
    bool restored = true;
    gdox_error restore_error;

    if (context->poisoned) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "RF02 optical session requires reopening");
        return false;
    }
    context->raw_cache_valid = false;
    context->raw_cache_bytes = 0U;
    for (index = 0U; index < 4U; ++index) {
        uint8_t confirm;
        if (!read_admission_byte(context, addresses[index], &saved[index], error)
            || !read_admission_byte(context, addresses[index], &confirm, error)) {
            return false;
        }
        if (saved[index] != 0U || confirm != saved[index]) {
            gdox_error_set(error, GDOX_ERROR_INVALID_SOURCE,
                "RF02 media admission fields are not in the validated Nintendo state");
            return false;
        }
    }
    uint32_t limit = lba;
    if (quality_scan) {
        limit = lba + 1024U;
        if (limit >= GDOX_GAMECUBE_TOTAL_SECTORS) {
            limit = GDOX_GAMECUBE_TOTAL_SECTORS - 1U;
        }
    }
    put_be_u24(desired, GDOX_GC_PHYSICAL_BASE + limit);
    desired[3] = 1U;
    for (index = 0U; index < 4U; ++index) {
        /* Count the attempted write before issuing it, including uncertain completion. */
        context->restore_pending |= (uint8_t)(1U << index);
        if (!write_admission_byte(context, addresses[index], desired[index], error)) {
            goto restore;
        }
    }
    if (!gdox_scsi_command_none(&context->transport, "RF02 admitted readiness check",
            tur, sizeof(tur), GDOX_GC_INQUIRY_TIMEOUT_MS, error)) {
        goto restore;
    }
    put_be_u32(cdb + 2U, lba);
    if (quality_scan) {
        uint8_t seek[10] = {0x2bU};
        static const uint8_t scan[12] = {
            0xf3U, 0x0eU, 0U, 0U, 0U, 0U, 0U, 0U, 0x10U, 0U, 0U, 0U,
        };
        uint8_t counters[16];
        put_be_u32(seek + 2U, lba);
        success = gdox_scsi_command_none(
            &context->transport, "RF02 GameCube scan seek", seek, sizeof(seek),
            GDOX_GC_READ_TIMEOUT_MS, error);
        if (success) {
            success = gdox_scsi_command_in(
                &context->transport, "RF02 GameCube quality scan", scan, sizeof(scan),
                counters, sizeof(counters), GDOX_GC_READ_TIMEOUT_MS, &transferred, error);
            if (success && transferred != 12U) {
                gdox_error_set(error, GDOX_ERROR_PROTOCOL,
                    "RF02 quality scan returned an unexpected counter length");
                success = false;
            }
        }
        if (!success && error->code == GDOX_ERROR_TRANSPORT) {
            context->poisoned = true;
        }
        goto restore;
    }
    success = gdox_scsi_command_in(
        &context->transport, "RF02 GameCube physical cache acquisition",
        cdb, sizeof(cdb), context->producer_buffer, GDOX_LOGICAL_SECTOR_BYTES,
        GDOX_GC_READ_TIMEOUT_MS, &transferred, error);
    if (!success && error->code == GDOX_ERROR_IO
        && last_sense_matches(context, 3U, 0x11U)
        && transferred == 0U) {
        /* The normal DVD decoder rejects Nintendo scrambling; authenticate RAM later. */
        gdox_error_clear(error);
        success = true;
    } else if (!success && error->code == GDOX_ERROR_TRANSPORT) {
        context->poisoned = true;
    }
restore:
    restored = restore_admission(context, &restore_error);
    if (!restored) {
        *error = restore_error;
    }
    if (!restored) {
        context->poisoned = true;
        context->session_present = false;
    }
    return success && restored;
}

static bool run_cache_producer(
    gdox_gc_context *context,
    uint32_t lba,
    gdox_error *error
)
{
    static const uint8_t start_cdb[12] = {
        0x1bU, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };
    uint8_t displacement_cdb[12] = {
        0xa8U, 0x08U, 0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U, 0U, 0U,
    };
    uint8_t streaming_cdb[12] = {
        0xa8U, 0U, 0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0x1bU, 0x80U, 0U,
    };
    size_t transferred;
    gdox_error completion;
    uint32_t displacement_lba;

    if (context->ready_gate) {
        return run_ready_cache_producer(context, lba, context->quality_scan, error);
    }
    if ((uint64_t)lba
        + GDOX_GC_METHOD4_DISPLACEMENT_SECTORS
        + GDOX_GC_SECTORS_PER_BLOCK
        <= GDOX_GAMECUBE_TOTAL_SECTORS) {
        displacement_lba =
            lba + GDOX_GC_METHOD4_DISPLACEMENT_SECTORS;
    } else {
        displacement_lba =
            lba - GDOX_GC_METHOD4_DISPLACEMENT_SECTORS;
    }
    put_be_u32(displacement_cdb + 2U, displacement_lba);
    put_be_u32(
        displacement_cdb + 6U,
        GDOX_GC_SECTORS_PER_BLOCK
    );
    put_be_u32(streaming_cdb + 2U, lba);
    if (context->producer_started) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "the consumed GameCube producer transaction has no validated coverage for this block"
        );
        return false;
    }
    context->raw_cache_valid = false;
    context->raw_cache_bytes = 0U;
    if (gdox_scsi_command_none(
            &context->transport,
            "START STOP UNIT (GameCube method-4 producer)",
            start_cdb,
            sizeof(start_cdb),
            GDOX_GC_METHOD4_TIMEOUT_MS,
            &completion
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "GameCube method-4 START unexpectedly completed GOOD"
        );
        return false;
    }
    if (!accept_cache_command_completion(context, &completion, error)) {
        return false;
    }
    context->producer_started = true;
    if (gdox_scsi_command_in(
            &context->transport,
            "READ(12) FUA (GameCube method-4 displacement)",
            displacement_cdb,
            sizeof(displacement_cdb),
            context->producer_buffer,
            GDOX_GC_METHOD4_DISPLACEMENT_BYTES,
            GDOX_GC_METHOD4_TIMEOUT_MS,
            &transferred,
            &completion
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "GameCube method-4 displacement unexpectedly completed GOOD"
        );
        return false;
    }
    if (transferred != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "GameCube method-4 displacement returned unexpected data"
        );
        return false;
    }
    if (!accept_cache_command_completion(context, &completion, error)) {
        return false;
    }
    if (gdox_scsi_command_in(
            &context->transport,
            "READ(12) STREAMING (GameCube method-4 target)",
            streaming_cdb,
            sizeof(streaming_cdb),
            context->producer_buffer,
            GDOX_GC_METHOD4_STREAM_BYTES,
            GDOX_GC_METHOD4_TIMEOUT_MS,
            &transferred,
            &completion
        )) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "GameCube method-4 STREAMING unexpectedly completed GOOD"
        );
        return false;
    }
    if (transferred != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_PROTOCOL,
            "GameCube method-4 STREAMING returned unexpected data"
        );
        return false;
    }
    if (!accept_cache_command_completion(context, &completion, error)) {
        return false;
    }
    if (!read_method4_cache(context, error)) {
        return false;
    }
    gdox_error_clear(error);
    return true;
}

static bool read_raw_cache(
    gdox_gc_context *context,
    uint32_t address,
    uint8_t *output,
    size_t output_bytes,
    gdox_error *error
)
{
    uint8_t cdb[12] = {0};
    size_t transferred;

    if (address > UINT32_C(0xffffff)
        || output == NULL
        || output_bytes == 0U
        || output_bytes > UINT32_C(0xffffff)) {
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, "invalid GP63 cache-buffer read");
        return false;
    }
    cdb[0] = 0x3cU;
    cdb[1] = 0x02U;
    put_be_u24(cdb + 3U, address);
    put_be_u24(cdb + 6U, (uint32_t)output_bytes);
    if (!gdox_scsi_command_in(
            &context->transport,
            "READ BUFFER (GP63 GameCube raw cache)",
            cdb,
            sizeof(cdb),
            output,
            output_bytes,
            GDOX_GC_METHOD4_TIMEOUT_MS,
            &transferred,
            error
        )) {
        return false;
    }
    if (transferred != output_bytes) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "GP63 raw cache returned a short transfer");
        return false;
    }
    return true;
}

static bool raw_block_identity_matches(
    const uint8_t raw[GDOX_GC_RAW_BLOCK_BYTES],
    uint32_t lba
)
{
    uint32_t index;
    for (index = 0U; index < GDOX_GC_SECTORS_PER_BLOCK; ++index) {
        if (!frame_identity_matches(
                raw + (size_t)index * GDOX_GC_CACHE_RECORD_BYTES,
                lba + index
            )) {
            return false;
        }
    }
    return true;
}

static bool capture_full_raw_cache(
    gdox_gc_context *context,
    gdox_error *error
)
{
    uint32_t address;

    context->raw_cache_valid = false;
    context->raw_cache_bytes = 0U;
    for (address = 0U;
         address < GDOX_GC_CACHE_SCAN_BYTES;
         address += GDOX_GC_CACHE_CHUNK_BYTES) {
        size_t chunk_bytes = GDOX_GC_CACHE_SCAN_BYTES - address;

        if (atomic_load_explicit(&context->abort, memory_order_acquire)) {
            gdox_error_set(
                error,
                GDOX_ERROR_CANCELLED,
                "GameCube optical session was cancelled"
            );
            return false;
        }
        if (chunk_bytes > GDOX_GC_CACHE_CHUNK_BYTES) {
            chunk_bytes = GDOX_GC_CACHE_CHUNK_BYTES;
        }
        if (!read_raw_cache(
                context,
                address,
                context->raw_cache + address,
                chunk_bytes,
                error
            )) {
            return false;
        }
    }
    context->raw_cache_bytes = GDOX_GC_CACHE_SCAN_BYTES;
    context->raw_cache_valid = true;
    return true;
}

/* DVD outer parity is RS(208,192) over GF(256), polynomial 0x11d,
 * roots alpha^0..alpha^15. The 172-byte tail of each physical record is
 * one of its sixteen parity rows. Correct at most eight symbol errors per
 * column, verifying all sixteen syndromes before applying any correction.
 * The caller still requires every corrected sector's identity and EDC. */
static size_t po_byte_offset(size_t row, size_t column)
{
    if (row < 192U) {
        return (row / 12U) * GDOX_GC_CACHE_RECORD_BYTES + (row % 12U) * 172U + column;
    }
    return (row - 192U) * GDOX_GC_CACHE_RECORD_BYTES + GDOX_GC_FRAME_BYTES + column;
}

static uint8_t gf_multiply(uint8_t a, uint8_t b,
    const uint8_t exponent[510], const uint8_t logarithm[256])
{
    return a != 0U && b != 0U ? exponent[(unsigned)logarithm[a] + logarithm[b]] : 0U;
}

/* Berlekamp-Massey locates errors; a small Vandermonde system recovers
 * their magnitudes. Fixed-size arrays bound work and memory. No erasures or
 * guessed data are supplied, and roots outside the shortened code reject. */
static bool correct_po_column(uint8_t raw[GDOX_GC_RAW_BLOCK_BYTES], size_t column,
    const uint8_t syndrome[16], const uint8_t exponent[510],
    const uint8_t logarithm[256])
{
    uint8_t locator[17] = {1U}, previous[17] = {1U};
    unsigned degree = 0U, shift = 1U;
    uint8_t last_discrepancy = 1U;
    for (unsigned n = 0U; n < 16U; ++n) {
        uint8_t discrepancy = syndrome[n];
        for (unsigned i = 1U; i <= degree; ++i) {
            discrepancy ^= gf_multiply(locator[i], syndrome[n - i], exponent, logarithm);
        }
        if (discrepancy == 0U) { ++shift; continue; }
        uint8_t saved[17];
        memcpy(saved, locator, sizeof(saved));
        const uint8_t scale = exponent[(unsigned)logarithm[discrepancy] + 255U
            - logarithm[last_discrepancy]];
        for (unsigned i = 0U; i + shift <= 16U; ++i) {
            locator[i + shift] ^= gf_multiply(scale, previous[i], exponent, logarithm);
        }
        if (2U * degree <= n) {
            degree = n + 1U - degree;
            memcpy(previous, saved, sizeof(previous));
            last_discrepancy = discrepancy;
            shift = 1U;
        } else {
            ++shift;
        }
    }
    if (degree == 0U || degree > 8U) return false;
    unsigned positions[8], powers[8], count = 0U;
    for (unsigned row = 0U; row < 208U; ++row) {
        const unsigned power = 207U - row;
        const uint8_t inverse = exponent[(255U - power) % 255U];
        uint8_t value = locator[degree];
        for (unsigned i = degree; i > 0U; --i) {
            value = gf_multiply(value, inverse, exponent, logarithm) ^ locator[i - 1U];
        }
        if (value != 0U) continue;
        if (count >= degree) return false;
        positions[count] = row;
        powers[count++] = power;
    }
    if (count != degree) return false;
    uint8_t matrix[8][9] = {{0}};
    for (unsigned row = 0U; row < count; ++row) {
        for (unsigned col = 0U; col < count; ++col) {
            matrix[row][col] = exponent[(row * powers[col]) % 255U];
        }
        matrix[row][count] = syndrome[row];
    }
    for (unsigned col = 0U; col < count; ++col) {
        unsigned pivot = col;
        while (pivot < count && matrix[pivot][col] == 0U) ++pivot;
        if (pivot == count) return false;
        if (pivot != col) {
            for (unsigned i = 0U; i <= count; ++i) {
                const uint8_t tmp = matrix[pivot][i];
                matrix[pivot][i] = matrix[col][i];
                matrix[col][i] = tmp;
            }
        }
        const uint8_t inverse = exponent[255U - logarithm[matrix[col][col]]];
        for (unsigned i = col; i <= count; ++i) {
            matrix[col][i] = gf_multiply(matrix[col][i], inverse, exponent, logarithm);
        }
        for (unsigned row = 0U; row < count; ++row) {
            if (row == col) continue;
            const uint8_t scale = matrix[row][col];
            for (unsigned i = col; i <= count; ++i) {
                matrix[row][i] ^= gf_multiply(scale, matrix[col][i], exponent, logarithm);
            }
        }
    }
    for (unsigned root = 0U; root < 16U; ++root) {
        uint8_t value = 0U;
        for (unsigned i = 0U; i < count; ++i) {
            value ^= gf_multiply(matrix[i][count], exponent[(root * powers[i]) % 255U],
                exponent, logarithm);
        }
        if (value != syndrome[root]) return false;
    }
    for (unsigned i = 0U; i < count; ++i) {
        raw[po_byte_offset(positions[i], column)] ^= matrix[i][count];
    }
    return true;
}

static bool correct_outer_parity(uint8_t raw[GDOX_GC_RAW_BLOCK_BYTES])
{
    uint8_t exponent[510];
    uint8_t logarithm[256] = {0};
    unsigned value = 1U;
    for (unsigned i = 0U; i < 255U; ++i) {
        exponent[i] = (uint8_t)value;
        logarithm[value] = (uint8_t)i;
        value <<= 1U;
        if ((value & 256U) != 0U) value ^= 0x11dU;
    }
    for (unsigned i = 255U; i < 510U; ++i) exponent[i] = exponent[i - 255U];
    for (size_t column = 0U; column < 172U; ++column) {
        uint8_t syndrome[16] = {0};
        uint8_t nonzero = 0U;
        for (size_t row = 0U; row < 208U; ++row) {
            const uint8_t symbol = raw[po_byte_offset(row, column)];
            for (unsigned root = 0U; root < 16U; ++root) {
                syndrome[root] = gf_multiply(syndrome[root], exponent[root],
                    exponent, logarithm) ^ symbol;
            }
        }
        for (unsigned root = 0U; root < 16U; ++root) nonzero |= syndrome[root];
        if (nonzero == 0U) continue;
        if (!correct_po_column(raw, column, syndrome, exponent, logarithm)) return false;
    }
    return true;
}

static bool locate_and_decode_cached_block(
    gdox_gc_context *context,
    uint32_t lba,
    gdox_error *error
)
{
    size_t record;
    size_t last_start;
    gdox_error validation_error;
    bool matched_identity = false;
    uint8_t assembled[GDOX_GC_RAW_BLOCK_BYTES];

    if (!context->raw_cache_valid
        || context->raw_cache_bytes < GDOX_GC_RAW_BLOCK_BYTES) {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "GP63 cache has no validated coverage for the requested GameCube block"
        );
        return false;
    }
    last_start =
        (context->raw_cache_bytes - GDOX_GC_RAW_BLOCK_BYTES)
            / GDOX_GC_CACHE_RECORD_BYTES;
    for (record = 0U; record <= last_start; ++record) {
        const uint8_t *raw =
            context->raw_cache
                + record * GDOX_GC_CACHE_RECORD_BYTES;

        if (atomic_load_explicit(&context->abort, memory_order_acquire)) {
            gdox_error_set(
                error,
                GDOX_ERROR_CANCELLED,
                "GameCube optical session was cancelled"
            );
            return false;
        }
        if (!raw_block_identity_matches(raw, lba)) {
            continue;
        }
        matched_identity = true;
        if (decode_block(context, raw, lba, &validation_error)) {
            return true;
        }
    }
    /* A block can cross the physical ring boundary. Reassemble its sixteen
     * identities before decoding; never assume host-buffer adjacency. */
    for (uint32_t sector = 0U; sector < GDOX_GC_SECTORS_PER_BLOCK; ++sector) {
        bool found = false;
        for (record = 0U;
             (record + 1U) * GDOX_GC_CACHE_RECORD_BYTES <= context->raw_cache_bytes;
             ++record) {
            const uint8_t *frame = context->raw_cache + record * GDOX_GC_CACHE_RECORD_BYTES;
            if (atomic_load_explicit(&context->abort, memory_order_acquire)) {
                gdox_error_set(error, GDOX_ERROR_CANCELLED,
                    "GameCube optical session was cancelled");
                return false;
            }
            if (frame_identity_matches(frame, lba + sector)) {
                memcpy(assembled + (size_t)sector * GDOX_GC_CACHE_RECORD_BYTES,
                    frame, GDOX_GC_CACHE_RECORD_BYTES);
                found = true;
                break;
            }
        }
        if (!found && lba + sector != 0U) {
            /* A damaged sector ID can hide a record. Its two consecutive
             * neighbors identify a candidate position only: retain the
             * original damaged bytes and require parity to repair the ID,
             * then validate all sixteen EDCs. Never synthesize a header. */
            for (record = 1U;
                 (record + 2U) * GDOX_GC_CACHE_RECORD_BYTES <= context->raw_cache_bytes;
                 ++record) {
                const uint8_t *frame = context->raw_cache + record * GDOX_GC_CACHE_RECORD_BYTES;
                if (frame_identity_matches(frame - GDOX_GC_CACHE_RECORD_BYTES, lba + sector - 1U)
                    && frame_identity_matches(frame + GDOX_GC_CACHE_RECORD_BYTES, lba + sector + 1U)) {
                    memcpy(assembled + (size_t)sector * GDOX_GC_CACHE_RECORD_BYTES,
                        frame, GDOX_GC_CACHE_RECORD_BYTES);
                    found = true;
                    break;
                }
            }
        }
        if (!found) goto unavailable;
    }
    matched_identity = true;
    if (decode_block(context, assembled, lba, &validation_error)) return true;
    if (correct_outer_parity(assembled)
        && decode_block(context, assembled, lba, &validation_error)) return true;
unavailable:
    if (matched_identity) {
        *error = validation_error;
    } else {
        gdox_error_set(
            error,
            GDOX_ERROR_IO,
            "GP63 cache has no validated coverage for the requested GameCube block"
        );
    }
    return false;
}

static bool read_decoded_block(
    gdox_gc_context *context,
    uint32_t lba,
    bool speculative,
    gdox_error *error
)
{
    const size_t block_index = lba / GDOX_GC_SECTORS_PER_BLOCK;
    const unsigned int attempts = speculative ? 1U : (context->quality_scan ? 3U : (context->ready_gate ? 2U : 1U));
    unsigned int attempt;
    gdox_error last;

    if (lba % GDOX_GC_SECTORS_PER_BLOCK != 0U
        || lba >= GDOX_GAMECUBE_TOTAL_SECTORS) {
        gdox_error_set(error, GDOX_ERROR_OUT_OF_BOUNDS, "invalid aligned GameCube cache read");
        return false;
    }
    if (decoded_block_is_valid(context, block_index)) {
        return true;
    }
    if (locate_and_decode_cached_block(context, lba, &last)) {
        return retain_decoded_block(context, block_index, error);
    }
    if (last.code == GDOX_ERROR_CANCELLED) {
        *error = last;
        return false;
    }
    if (atomic_load_explicit(&context->abort, memory_order_acquire)) {
        gdox_error_set(error, GDOX_ERROR_CANCELLED, "GameCube optical session was cancelled");
        return false;
    }
    for (attempt = 0U; attempt < attempts; ++attempt) {
        if (atomic_load_explicit(&context->abort, memory_order_acquire)) {
            gdox_error_set(error, GDOX_ERROR_CANCELLED, "GameCube optical session was cancelled");
            return false;
        }
        /* Quality scanning can skip incomplete physical coverage. A final
         * normal acquisition gets the drive's full recovery path. Every
         * attempt is restored and independently validated before use. */
        const bool acquired = context->quality_scan && attempt == 2U
            ? run_ready_cache_producer(context, lba, false, &last)
            : run_cache_producer(context, lba, &last);
        if (!acquired
            || !capture_full_raw_cache(context, &last)) {
            if (speculative && last.code != GDOX_ERROR_CANCELLED) context->poisoned = true;
            break;
        }
        if (locate_and_decode_cached_block(context, lba, &last)) {
            return retain_decoded_block(context, block_index, error);
        }
        /* Retry only after a complete transaction and restored RAM state.
         * Missing coverage can be transient too. Transport, restoration and
         * capture failures already stop above; cancellation never retries. */
        if (last.code != GDOX_ERROR_INVALID_SOURCE && last.code != GDOX_ERROR_IO) {
            break;
        }
    }
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    (void)snprintf(message, sizeof(message), "GameCube block LBA %u: %.300s",
        (unsigned)lba, last.message);
    gdox_error_set(error, last.code, message);
    return false;
}

static uint64_t gc_sector_count(const void *raw_context)
{
    (void)raw_context;
    return GDOX_GAMECUBE_TOTAL_SECTORS;
}

static void schedule_readahead(gdox_gc_context *context, uint32_t lba, uint32_t blocks)
{
    if (!context->worker_started) return;
    const uint32_t end = lba + blocks;
    gdox_mutex_lock(&context->cache_mutex);
    size_t slot = GDOX_GC_STREAMS;
    for (size_t i = 0U; i < GDOX_GC_STREAMS; ++i) {
        const uint32_t previous = context->streams[i].last_end;
        if (context->streams[i].valid && lba <= previous && lba + 1U >= previous) {
            slot = i;
            break;
        }
    }
    const bool sequential = slot != GDOX_GC_STREAMS;
    /* Dolphin splits movie packets into short tails and aligned reads.
     * Those tails must advance an existing stream, or each split silently
     * breaks sequential detection. Small unrelated metadata reads should
     * neither start new speculation nor evict an established stream. */
    if ((!sequential && blocks < 8U)
        || (sequential && end <= context->streams[slot].last_end)) {
        gdox_mutex_unlock(&context->cache_mutex);
        return;
    }
    if (!sequential) {
        slot = 0U;
        for (size_t i = 0U; i < GDOX_GC_STREAMS; ++i) {
            if (!context->streams[i].valid) { slot = i; break; }
            if (context->streams[i].used < context->streams[slot].used) slot = i;
        }
    }
    gdox_gc_stream *stream = &context->streams[slot];
    const uint32_t next = (end + 15U) / 16U * 16U;
    if (!sequential) stream->retry_after = 0U;
    if (!sequential || stream->next < next) stream->next = next;
    stream->last_end = end;
    /* A rejected speculative block waits for demand recovery. Resume right
     * after that block, not at the far end of the old speculative window. */
    if (next >= stream->retry_after) stream->retry_after = 0U;
    stream->end = sequential && stream->retry_after == 0U
        ? next + GDOX_GC_READAHEAD_SECTORS : stream->next;
    if (stream->end > GDOX_GAMECUBE_TOTAL_SECTORS) stream->end = GDOX_GAMECUBE_TOTAL_SECTORS;
    stream->used = ++context->use_counter;
    stream->valid = true;
    ++stream->generation;
    gdox_mutex_unlock(&context->cache_mutex);
}

static void *readahead_worker(void *raw_context)
{
    gdox_gc_context *context = raw_context;
    for (;;) {
        size_t selected = GDOX_GC_STREAMS;
        uint32_t lba = 0U;
        uint64_t generation = 0U;
        gdox_mutex_lock(&context->cache_mutex);
        while (!atomic_load(&context->abort) && !atomic_load(&context->poisoned)) {
            if (atomic_load(&context->demand_reads) == 0U) {
                for (unsigned i = 0U; i < GDOX_GC_STREAMS; ++i) {
                    gdox_gc_stream *stream = &context->streams[i];
                    if (stream->valid && stream->next < stream->end
                        && (selected == GDOX_GC_STREAMS
                            || stream->used > context->streams[selected].used)) {
                        selected = i;
                        lba = stream->next;
                        generation = stream->generation;
                    }
                }
            }
            if (selected != GDOX_GC_STREAMS) break;
            pthread_cond_wait(&context->work_ready, &context->cache_mutex);
        }
        gdox_mutex_unlock(&context->cache_mutex);
        if (atomic_load(&context->abort) || atomic_load(&context->poisoned)) break;
        gdox_mutex_lock(&context->mutex);
        if (atomic_load(&context->demand_reads) != 0U) {
            gdox_mutex_unlock(&context->mutex);
            continue;
        }
        gdox_error error;
        const bool success = read_decoded_block(context, lba, true, &error);
        if (!success && atomic_load(&context->poisoned)) {
            (void)fprintf(stderr, "GDOX: GameCube read-ahead stopped at sector %u: %s\n",
                lba, error.message);
        }
        /* Speculation never uses the slow normal-read recovery. A rejected
         * block stops this window; a transport/restoration fault poisons it. */
        if (!success && error.code != GDOX_ERROR_INVALID_SOURCE
            && error.code != GDOX_ERROR_IO && error.code != GDOX_ERROR_CANCELLED) {
            atomic_store(&context->poisoned, true);
            (void)fprintf(stderr, "GDOX: GameCube transport failed at sector %u: %s\n",
                lba, error.message);
        }
        gdox_mutex_unlock(&context->mutex);
        gdox_mutex_lock(&context->cache_mutex);
        gdox_gc_stream *stream = &context->streams[selected];
        if (stream->generation == generation) {
            stream->next = lba + GDOX_GC_SECTORS_PER_BLOCK;
            if (!success) {
                stream->retry_after = stream->next;
                stream->end = stream->next;
            }
        }
        gdox_mutex_unlock(&context->cache_mutex);
    }
    return NULL;
}

static bool session_readable(gdox_gc_context *context, gdox_error *error)
{
    if (atomic_load(&context->abort)) {
        gdox_error_set(error, GDOX_ERROR_CANCELLED, "GameCube optical session was cancelled");
        return false;
    }
    if (atomic_load(&context->poisoned) || !atomic_load(&context->session_present)) {
        gdox_error_set(error, GDOX_ERROR_TRANSPORT, "GameCube optical session is unavailable");
        return false;
    }
    return true;
}

static bool gc_read(
    void *raw_context, uint64_t lba, uint32_t blocks,
    uint8_t *output, size_t output_bytes, gdox_error *error
)
{
    gdox_gc_context *context = raw_context;
    uint64_t current = lba;
    uint32_t remaining = blocks;
    size_t output_offset = 0U;
    bool success = true;
    if (!gdox_source_validate_read(GDOX_GAMECUBE_TOTAL_SECTORS,
            lba, blocks, output_bytes, error)) return false;
    atomic_fetch_add(&context->demand_reads, 1U);
    while (remaining != 0U) {
        if (!session_readable(context, error)) { success = false; break; }
        const uint32_t base = (uint32_t)(current / 16U) * 16U;
        const uint32_t offset = (uint32_t)current - base;
        const size_t block_index = base / 16U;
        uint32_t count = 16U - offset;
        if (count > remaining) count = remaining;
        const size_t bytes = (size_t)count * GDOX_LOGICAL_SECTOR_BYTES;
        if (!copy_decoded(context, block_index, (size_t)offset * GDOX_LOGICAL_SECTOR_BYTES,
                bytes, output + output_offset)) {
            gdox_mutex_lock(&context->mutex);
            success = session_readable(context, error)
                && read_decoded_block(context, base, false, error);
            if (success) {
                success = copy_decoded(context, block_index,
                    (size_t)offset * GDOX_LOGICAL_SECTOR_BYTES, bytes, output + output_offset);
                if (!success) gdox_error_set(error, GDOX_ERROR_INTERNAL, "lost decoded demand block");
            } else if (error->code == GDOX_ERROR_TRANSPORT || error->code == GDOX_ERROR_NOT_FOUND) {
                atomic_store(&context->session_present, false);
            }
            gdox_mutex_unlock(&context->mutex);
            if (!success) break;
        }
        current += count;
        remaining -= count;
        output_offset += bytes;
    }
    if (success) success = session_readable(context, error);
    if (success) schedule_readahead(context, (uint32_t)lba, blocks);
    atomic_fetch_sub(&context->demand_reads, 1U);
    gdox_mutex_lock(&context->cache_mutex);
    pthread_cond_signal(&context->work_ready);
    gdox_mutex_unlock(&context->cache_mutex);
    return success;
}

static bool gc_media_present(const void *raw_context)
{
    const gdox_gc_context *context = raw_context;
    return atomic_load(&context->session_present) && !atomic_load(&context->poisoned)
        && !atomic_load(&context->abort);
}

static void gc_abort(void *raw_context)
{
    gdox_gc_context *context = raw_context;
    atomic_store_explicit(&context->abort, true, memory_order_release);
    gdox_mutex_lock(&context->cache_mutex);
    pthread_cond_broadcast(&context->work_ready);
    gdox_mutex_unlock(&context->cache_mutex);
}

static bool gc_prepare_close(void *raw_context, gdox_error *error)
{
    gdox_gc_context *context = raw_context;
    gc_abort(context);
    if (context->worker_started) {
        if (pthread_join(context->worker, NULL) != 0) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not drain GameCube reads");
            return false;
        }
        context->worker_started = false;
    }
    return restore_admission(context, error)
        && gdox_scsi_transport_prepare_close(&context->transport, error);
}

static bool gc_close(void *raw_context, gdox_error *error)
{
    gdox_gc_context *context = raw_context;
    const bool closed = gdox_scsi_transport_close(&context->transport, error);
    pthread_cond_destroy(&context->work_ready);
    gdox_mutex_destroy(&context->cache_mutex);
    gdox_mutex_destroy(&context->mutex);
    free(context->decoded_cache);
    free(context->raw_cache);
    free(context);
    return closed;
}

static const gdox_sector_source_ops gc_source_ops = {
    gc_sector_count,
    gc_read,
    gc_media_present,
    gc_close,
    NULL,
    NULL,
    gc_abort,
    gc_prepare_close,
    NULL,
};

static void destroy_failed_open(gdox_gc_context *context, gdox_sector_source *source)
{
    gdox_error ignored;
    if (context->restore_pending != 0U) {
        /* A failed open can still own changed drive state. Let the caller retry
         * cleanup through the normal source lifetime; never discard the mask. */
        source->context = context;
        source->ops = &gc_source_ops;
        return;
    }
    if (gdox_scsi_transport_is_valid(&context->transport)) {
        (void)gdox_scsi_transport_close(&context->transport, &ignored);
    }
    pthread_cond_destroy(&context->work_ready);
    gdox_mutex_destroy(&context->cache_mutex);
    gdox_mutex_destroy(&context->mutex);
    free(context->decoded_cache);
    free(context->raw_cache);
    free(context);
}

static bool open_gamecube_source(
    gdox_gp63_gamecube_transport_opener opener,
    void *opener_context,
    uint8_t read_retries,
    uint32_t ready_timeout_ms,
    bool ready_gate,
    bool quality_scan,
    bool readahead,
    gdox_sector_source *source,
    gdox_error *error
)
{
    gdox_gc_context *context;
    gdox_gc_identity identity;

    (void)ready_timeout_ms;
    gdox_error_clear(error);
    if (opener == NULL || source == NULL || gdox_source_is_valid(source)) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "a GP63 GameCube transport opener and empty source are required"
        );
        return false;
    }
    context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate GameCube optical source");
        return false;
    }
    atomic_init(&context->abort, false);
    atomic_init(&context->poisoned, false);
    atomic_init(&context->session_present, false);
    atomic_init(&context->demand_reads, 0U);
    context->ready_gate = ready_gate;
    context->quality_scan = quality_scan;
    (void)read_retries;
    context->decoded_cache = malloc(GDOX_GC_BUFFER_BLOCKS * GDOX_GC_LOGICAL_BLOCK_BYTES);
    context->raw_cache = malloc(GDOX_GC_CACHE_SCAN_BYTES);
    if (context->decoded_cache == NULL || context->raw_cache == NULL) {
        free(context->decoded_cache);
        free(context->raw_cache);
        free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not allocate GameCube RAM read buffers");
        return false;
    }
    initialize_edc_table(context->edc_table);
    if (!gdox_mutex_init(&context->mutex)) {
        free(context->decoded_cache);
        free(context->raw_cache);
        free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize GameCube source lock");
        return false;
    }
    if (!gdox_mutex_init(&context->cache_mutex)) {
        gdox_mutex_destroy(&context->mutex);
        free(context->decoded_cache); free(context->raw_cache); free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize GameCube RAM lock");
        return false;
    }
    if (pthread_cond_init(&context->work_ready, NULL) != 0) {
        gdox_mutex_destroy(&context->cache_mutex); gdox_mutex_destroy(&context->mutex);
        free(context->decoded_cache); free(context->raw_cache); free(context);
        gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not initialize GameCube read-ahead signal");
        return false;
    }
    if (!opener(opener_context, &context->transport, error)
        || !inquiry(&context->transport, &identity, error)) {
        destroy_failed_open(context, source);
        return false;
    }
    if (!identity_is_gp63(&identity)) {
        destroy_failed_open(context, source);
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "USB device is not the validated HL-DT-ST GP63EX70 RF02 mechanism"
        );
        return false;
    }
    if (!read_decoded_block(context, 0U, false, error)) {
        if (error->code == GDOX_ERROR_INVALID_SOURCE
            || error->code == GDOX_ERROR_IO) {
            error->code = GDOX_ERROR_UNSUPPORTED;
        }
        destroy_failed_open(context, source);
        return false;
    }
    if (!load_decoded_block(context, 0U, error)) {
        destroy_failed_open(context, source);
        return false;
    }
    if (read_be_u32(context->decode_buffer + 0x1cU)
        != GDOX_GC_MAGIC) {
        gdox_error_set(
            error,
            GDOX_ERROR_UNSUPPORTED,
            "profile-zero media did not contain the GameCube disc magic"
        );
        destroy_failed_open(context, source);
        return false;
    }
    context->session_present = true;
    if (readahead) {
        if (pthread_create(&context->worker, NULL, readahead_worker, context) != 0) {
            gdox_error_set(error, GDOX_ERROR_INTERNAL, "could not start GameCube read-ahead worker");
            destroy_failed_open(context, source);
            return false;
        }
        context->worker_started = true;
    }
    source->context = context;
    source->ops = &gc_source_ops;
    return true;
}

bool gdox_gp63_gamecube_source_open(
    gdox_gp63_gamecube_transport_opener opener, void *opener_context,
    uint8_t read_retries, uint32_t ready_timeout_ms,
    gdox_sector_source *source, gdox_error *error
)
{
    return open_gamecube_source(opener, opener_context, read_retries,
        ready_timeout_ms, false, false, false, source, error);
}

bool gdox_gp63_gamecube_ready_source_open(
    gdox_gp63_gamecube_transport_opener opener, void *opener_context,
    gdox_sector_source *source, gdox_error *error
)
{
    return open_gamecube_source(opener, opener_context, 0U, 0U, true, false, false, source, error);
}

bool gdox_gp63_gamecube_scan_source_open(
    gdox_gp63_gamecube_transport_opener opener, void *opener_context,
    gdox_sector_source *source, gdox_error *error
)
{
    return open_gamecube_source(opener, opener_context, 0U, 0U, true, true, false, source, error);
}

bool gdox_gp63_gamecube_stream_source_open(
    gdox_gp63_gamecube_transport_opener opener, void *opener_context,
    gdox_sector_source *source, gdox_error *error
)
{
    return open_gamecube_source(opener, opener_context, 0U, 0U, true, true, true, source, error);
}
