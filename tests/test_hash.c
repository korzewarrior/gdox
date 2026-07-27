#include "test.h"

#include "gdox/hash.h"

#include <stdio.h>
#include <string.h>

static void format_crc32(uint32_t crc32, char output[9])
{
    (void)snprintf(output, 9U, "%08X", crc32);
}

void gdox_test_hash(void)
{
    static const uint8_t input[] = {'a', 'b', 'c'};
    gdox_hash_stream *stream = NULL;
    gdox_hashes hashes;
    gdox_error error;
    char crc32[9];
    char md5[GDOX_MD5_BYTES * 2U + 1U];
    char sha1[GDOX_SHA1_BYTES * 2U + 1U];
    char sha256[GDOX_SHA256_BYTES * 2U + 1U];

    GDOX_TEST_CHECK(gdox_hash_stream_create(&stream, &error));
    GDOX_TEST_CHECK(gdox_hash_stream_update(stream, input, 1U, &error));
    GDOX_TEST_CHECK(gdox_hash_stream_update(stream, input + 1U, 2U, &error));
    GDOX_TEST_CHECK(gdox_hash_stream_finish(stream, &hashes, &error));
    gdox_hash_stream_destroy(stream);

    format_crc32(hashes.crc32, crc32);
    gdox_hash_hex(hashes.md5, sizeof(hashes.md5), false, md5);
    gdox_hash_hex(hashes.sha1, sizeof(hashes.sha1), false, sha1);
    gdox_hash_hex(hashes.sha256, sizeof(hashes.sha256), false, sha256);
    GDOX_TEST_CHECK(strcmp(crc32, "352441C2") == 0);
    GDOX_TEST_CHECK(strcmp(md5, "900150983cd24fb0d6963f7d28e17f72") == 0);
    GDOX_TEST_CHECK(strcmp(sha1, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    GDOX_TEST_CHECK(strcmp(
        sha256,
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad"
    ) == 0);
}
