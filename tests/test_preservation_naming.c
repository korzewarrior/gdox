#include "test.h"

#include "app/preservation_naming.h"

#include <string.h>

void gdox_test_preservation_naming(void)
{
    char filename[GDOX_PRESERVATION_FILENAME_CAPACITY];
    char tiny[8];

    GDOX_TEST_CHECK(gdox_preservation_suggest_filename(
        "Halo: Combat Evolved",
        GDOX_PRESERVATION_XISO_COMPACT,
        filename,
        sizeof(filename)
    ));
    GDOX_TEST_CHECK(strcmp(filename, "Halo Combat Evolved-xiso.iso") == 0);

    GDOX_TEST_CHECK(gdox_preservation_suggest_filename(
        "  Jet Set Radio Future...  ",
        GDOX_PRESERVATION_REDUMP,
        filename,
        sizeof(filename)
    ));
    GDOX_TEST_CHECK(
        strcmp(filename, "Jet Set Radio Future-full-disc.iso") == 0
    );

    GDOX_TEST_CHECK(gdox_preservation_suggest_filename(
        "Tom Clancy's Splinter Cell / Pandora Tomorrow",
        GDOX_PRESERVATION_XISO_COMPACT,
        filename,
        sizeof(filename)
    ));
    GDOX_TEST_CHECK(
        strcmp(
            filename,
            "Tom Clancy's Splinter Cell Pandora Tomorrow-xiso.iso"
        ) == 0
    );

    GDOX_TEST_CHECK(gdox_preservation_suggest_filename(
        "",
        GDOX_PRESERVATION_XISO_COMPACT,
        filename,
        sizeof(filename)
    ));
    GDOX_TEST_CHECK(strcmp(filename, "Xbox game-xiso.iso") == 0);

    GDOX_TEST_CHECK(!gdox_preservation_suggest_filename(
        "Halo",
        GDOX_PRESERVATION_XISO_COMPACT,
        tiny,
        sizeof(tiny)
    ));
    GDOX_TEST_CHECK(tiny[0] == '\0');
}
