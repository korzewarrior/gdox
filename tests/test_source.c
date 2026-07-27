#include "test.h"

#include "gdox/source.h"

void gdox_test_source(void)
{
    gdox_error error;

    GDOX_TEST_CHECK(
        gdox_source_validate_read(
            UINT64_C(100),
            UINT64_C(99),
            UINT32_C(1),
            GDOX_LOGICAL_SECTOR_BYTES,
            &error
        )
    );
    GDOX_TEST_CHECK(
        !gdox_source_validate_read(
            UINT64_C(100),
            UINT64_C(100),
            UINT32_C(1),
            GDOX_LOGICAL_SECTOR_BYTES,
            &error
        )
    );
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_OUT_OF_BOUNDS);
    GDOX_TEST_CHECK(
        !gdox_source_validate_read(
            UINT64_C(100),
            UINT64_C(0),
            UINT32_C(1),
            GDOX_LOGICAL_SECTOR_BYTES - 1U,
            &error
        )
    );
    GDOX_TEST_CHECK(error.code == GDOX_ERROR_PROTOCOL);
}
