#pragma once

#include <error_code.h>
#include <stddef.h>
#include <stdint.h>

typedef struct OkiAdpcm OkiAdpcm;

OkiAdpcm *OkiAdpcm_Create(int output_bits, ErrorCode *error);
void OkiAdpcm_Destroy(OkiAdpcm *self);
void OkiAdpcm_Reset(OkiAdpcm *self);
size_t OkiAdpcm_Decode(OkiAdpcm *self, const uint8_t *adpcm, size_t length, int16_t *out);
