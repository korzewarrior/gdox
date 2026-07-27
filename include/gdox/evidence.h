#ifndef GDOX_EVIDENCE_H
#define GDOX_EVIDENCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDOX_DISC_STRUCTURE_BYTES 2048U
#define GDOX_EVIDENCE_NOTE_CAPACITY 256U

typedef struct gdox_disc_evidence {
    bool pfi_present;
    bool dmi_present;
    bool security_sector_present;
    uint8_t pfi[GDOX_DISC_STRUCTURE_BYTES];
    uint8_t dmi[GDOX_DISC_STRUCTURE_BYTES];
    uint8_t security_sector[GDOX_DISC_STRUCTURE_BYTES];
    char note[GDOX_EVIDENCE_NOTE_CAPACITY];
} gdox_disc_evidence;

void gdox_disc_evidence_clear(gdox_disc_evidence *evidence);

#ifdef __cplusplus
}
#endif

#endif
