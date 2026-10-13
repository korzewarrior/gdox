#include "platform/gp63_sg_linux.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static void test_fixed_commands(void)
{
    uint8_t scan[12] = {0xf3U, 0x0eU, 0U, 0U, 0U, 0U, 0U, 0U, 0x10U};
    uint8_t seek[10] = {0x2bU};
    CHECK(gdox_gp63_sg_linux_command_allowed(scan, sizeof(scan), true, 16U, 30000U));
    CHECK(!gdox_gp63_sg_linux_command_allowed(scan, sizeof(scan), false, 16U, 30000U));
    CHECK(!gdox_gp63_sg_linux_command_allowed(scan, sizeof(scan), true, 12U, 30000U));
    scan[1] = 0x0dU;
    CHECK(!gdox_gp63_sg_linux_command_allowed(scan, sizeof(scan), true, 16U, 30000U));
    CHECK(gdox_gp63_sg_linux_command_allowed(seek, sizeof(seek), false, 0U, 30000U));
    seek[5] = 1U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(seek, sizeof(seek), false, 0U, 30000U));
    put_be_u32(seek + 2U, 712880U);
    CHECK(!gdox_gp63_sg_linux_command_allowed(seek, sizeof(seek), false, 0U, 30000U));
    uint8_t inquiry[6] = {0x12U, 0U, 0U, 0U, 36U, 0U};
    uint8_t configuration[10] = {
        0x46U, 0x01U, 0U, 0U, 0U, 0U, 0U, 0U, 8U, 0U,
    };
    uint8_t start[12] = {
        0x1bU, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };

    CHECK(gdox_gp63_sg_linux_command_allowed(
        inquiry,
        sizeof(inquiry),
        true,
        36U,
        UINT32_C(10000)
    ));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        configuration,
        sizeof(configuration),
        true,
        8U,
        UINT32_C(5000)
    ));
    configuration[1] = 2U;
    CHECK(gdox_gp63_sg_linux_command_allowed(
        configuration, sizeof(configuration), true, 8U, UINT32_C(10000)
    ));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        configuration, sizeof(configuration), false, 8U, UINT32_C(10000)
    ));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        configuration, sizeof(configuration), true, 9U, UINT32_C(10000)
    ));
    configuration[8] = 9U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        configuration, sizeof(configuration), true, 9U, UINT32_C(10000)
    ));
    CHECK(gdox_gp63_sg_linux_command_allowed(
        start,
        sizeof(start),
        false,
        0U,
        UINT32_C(100000)
    ));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        start,
        sizeof(start),
        true,
        0U,
        UINT32_C(100000)
    ));
    inquiry[4] = 96U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        inquiry,
        sizeof(inquiry),
        true,
        36U,
        UINT32_C(10000)
    ));
    start[4] = 3U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        start,
        sizeof(start),
        false,
        0U,
        UINT32_C(100000)
    ));
}

static void test_only_fixed_deferred_sense_is_accepted(void)
{
    uint8_t sense[18] = {
        0x71U, 0U, 0x02U, 0U, 0U, 0U, 0U, 10U,
        0U, 0U, 0U, 0U, 0x30U, 0U, 0U, 0U, 0U, 0U,
    };
    uint8_t key = 0U;
    uint8_t asc = 0U;
    uint8_t ascq = 0U;

    CHECK(gdox_gp63_sg_linux_parse_deferred_sense(
        sense,
        sizeof(sense),
        &key,
        &asc,
        &ascq
    ));
    CHECK(key == 0x02U);
    CHECK(asc == 0x30U);
    CHECK(ascq == 0x00U);

    sense[0] = 0x70U;
    CHECK(!gdox_gp63_sg_linux_parse_deferred_sense(
        sense,
        sizeof(sense),
        &key,
        &asc,
        &ascq
    ));
    sense[0] = 0x72U;
    CHECK(!gdox_gp63_sg_linux_parse_deferred_sense(
        sense,
        sizeof(sense),
        &key,
        &asc,
        &ascq
    ));
    sense[0] = 0x73U;
    CHECK(!gdox_gp63_sg_linux_parse_deferred_sense(
        sense,
        sizeof(sense),
        &key,
        &asc,
        &ascq
    ));
    sense[0] = 0x71U;
    sense[7] = 11U;
    CHECK(!gdox_gp63_sg_linux_parse_deferred_sense(
        sense,
        sizeof(sense),
        &key,
        &asc,
        &ascq
    ));
    CHECK(!gdox_gp63_sg_linux_parse_deferred_sense(
        sense,
        13U,
        &key,
        &asc,
        &ascq
    ));
    CHECK(!gdox_gp63_sg_linux_parse_deferred_sense(
        NULL,
        0U,
        &key,
        &asc,
        &ascq
    ));
}

static void test_acquisition_commands(void)
{
    uint8_t displacement[12] = {0xa8U, 0x08U};
    uint8_t streaming[12] = {0xa8U};
    uint8_t cache[12] = {0x3cU, 0x02U};
    uint8_t method4_cache[12] = {0x3cU, 0x02U};

    put_be_u32(displacement + 2U, UINT32_C(0x03e0));
    put_be_u32(displacement + 6U, UINT32_C(16));
    CHECK(gdox_gp63_sg_linux_command_allowed(
        displacement,
        sizeof(displacement),
        true,
        UINT32_C(65536),
        UINT32_C(100000)
    ));
    displacement[5] = 1U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        displacement,
        sizeof(displacement),
        true,
        UINT32_C(65536),
        UINT32_C(100000)
    ));
    put_be_u32(displacement + 2U, UINT32_C(712864));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        displacement,
        sizeof(displacement),
        true,
        UINT32_C(65536),
        UINT32_C(100000)
    ));
    put_be_u32(displacement + 2U, UINT32_C(712880));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        displacement,
        sizeof(displacement),
        true,
        UINT32_C(65536),
        UINT32_C(100000)
    ));

    put_be_u32(streaming + 2U, 0U);
    put_be_u32(streaming + 6U, UINT32_C(0x1b));
    streaming[10] = 0x80U;
    CHECK(gdox_gp63_sg_linux_command_allowed(
        streaming,
        sizeof(streaming),
        true,
        UINT32_C(55728),
        UINT32_C(100000)
    ));
    put_be_u32(streaming + 2U, UINT32_C(16));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        streaming,
        sizeof(streaming),
        true,
        UINT32_C(55728),
        UINT32_C(100000)
    ));
    put_be_u32(streaming + 2U, 0U);
    streaming[10] = 0U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        streaming,
        sizeof(streaming),
        true,
        UINT32_C(55728),
        UINT32_C(100000)
    ));

    put_be_u24(method4_cache + 6U, UINT32_C(55728));
    CHECK(gdox_gp63_sg_linux_command_allowed(
        method4_cache,
        sizeof(method4_cache),
        true,
        UINT32_C(55728),
        UINT32_C(30000)
    ));
    method4_cache[10] = 1U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        method4_cache,
        sizeof(method4_cache),
        true,
        UINT32_C(55728),
        UINT32_C(30000)
    ));
    put_be_u24(cache + 6U, UINT32_C(0x010000));
    CHECK(gdox_gp63_sg_linux_command_allowed(
        cache,
        sizeof(cache),
        true,
        UINT32_C(0x010000),
        UINT32_C(100000)
    ));
    put_be_u24(cache + 3U, UINT32_C(0x0b0000));
    CHECK(gdox_gp63_sg_linux_command_allowed(
        cache,
        sizeof(cache),
        true,
        UINT32_C(0x010000),
        UINT32_C(100000)
    ));
    put_be_u24(cache + 3U, UINT32_C(0x0c0000));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        cache,
        sizeof(cache),
        true,
        UINT32_C(0x010000),
        UINT32_C(30000)
    ));
    put_be_u24(cache + 3U, UINT32_C(0x0a8000));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        cache,
        sizeof(cache),
        true,
        UINT32_C(0x010000),
        UINT32_C(30000)
    ));
    put_be_u24(cache + 3U, 0U);
    put_be_u24(cache + 6U, UINT32_C(0x0c0000));
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        cache,
        sizeof(cache),
        true,
        UINT32_C(0x0c0000),
        UINT32_C(30000)
    ));
    put_be_u24(cache + 6U, UINT32_C(0x010000));
    cache[5] = 1U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(
        cache,
        sizeof(cache),
        true,
        UINT32_C(0x010000),
        UINT32_C(30000)
    ));
}

static void test_ready_gate_command_boundaries(void)
{
    uint8_t field[10] = {0xf1U, 1U, 0U, 0U, 3U, 0x7eU, 0U, 0U, 0U, 1U};
    uint8_t read[12] = {0xa8U, 0U, 0U, 0U, 0x10U, 0U, 0U, 0U, 0U, 1U, 0x80U, 0U};
    uint8_t sense[18] = {0x71U, 0U, 3U, 0U, 0U, 0U, 0U, 10U, 0U, 0U, 0U, 0U, 0x11U};
    uint8_t key, asc, ascq;
    CHECK(gdox_gp63_sg_linux_command_allowed(field, sizeof(field), false, 0U, 10000U));
    field[9] = 2U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(field, sizeof(field), false, 0U, 10000U));
    field[9] = 0U;
    field[4] = 0x4eU;
    field[5] = 0x0aU;
    CHECK(!gdox_gp63_sg_linux_command_allowed(field, sizeof(field), false, 0U, 10000U));
    field[4] = 0x85U;
    field[5] = 0x38U;
    field[1] = 2U;
    field[6] = 1U;
    CHECK(gdox_gp63_sg_linux_command_allowed(field, sizeof(field), true, 4U, 10000U));
    field[5] = 0x37U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(field, sizeof(field), true, 4U, 10000U));
    CHECK(gdox_gp63_sg_linux_command_allowed(read, sizeof(read), true, 2048U, 30000U));
    read[5] = 1U;
    CHECK(!gdox_gp63_sg_linux_command_allowed(read, sizeof(read), true, 2048U, 30000U));
    CHECK(gdox_gp63_sg_linux_parse_deferred_sense(sense, sizeof(sense), &key, &asc, &ascq));
    CHECK(key == 3U && asc == 0x11U && ascq == 0U);
    sense[12] = 0x30U;
    CHECK(!gdox_gp63_sg_linux_parse_deferred_sense(sense, sizeof(sense), &key, &asc, &ascq));
}

int main(void)
{
    test_ready_gate_command_boundaries();
    test_fixed_commands();
    test_only_fixed_deferred_sense_is_accepted();
    test_acquisition_commands();
    if (failures != 0) {
        (void)fprintf(stderr, "%d Linux SG_IO policy check(s) failed\n", failures);
        return 1;
    }
    (void)puts("GDOX GP63 Linux SG_IO policy tests passed");
    return 0;
}
