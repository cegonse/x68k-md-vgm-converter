#include <oki_adpcm.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  OKI_STEP_MIN = 0,
  OKI_STEP_MAX = 48,
  OKI_STEP_COUNT = 49,
  OKI_SIGNAL_INIT = -2,
  OKI_OUTPUT_BITS_10 = 10,
  OKI_OUTPUT_BITS_12 = 12
};

struct OkiAdpcm {
  int32_t signal;
  int step;
  int32_t output_mask;
};

static const int index_shift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static int diff_lookup[OKI_STEP_COUNT * 16];
static bool tables_computed = false;

static void computeTables(void) {
  static const int nbl2bit[16][4] = {
      {1, 0, 0, 0},  {1, 0, 0, 1},  {1, 0, 1, 0},  {1, 0, 1, 1},
      {1, 1, 0, 0},  {1, 1, 0, 1},  {1, 1, 1, 0},  {1, 1, 1, 1},
      {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
      {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}};

  if (tables_computed) {
    return;
  }
  for (int step = 0; step < OKI_STEP_COUNT; step++) {
    int stepval = (int)floor(16.0 * pow(11.0 / 10.0, (double)step));
    for (int nib = 0; nib < 16; nib++) {
      diff_lookup[step * 16 + nib] =
          nbl2bit[nib][0] * (stepval * nbl2bit[nib][1] +
                             stepval / 2 * nbl2bit[nib][2] +
                             stepval / 4 * nbl2bit[nib][3] +
                             stepval / 8);
    }
  }
  tables_computed = true;
}

static int16_t decodeNibble(OkiAdpcm *self, uint8_t nibble) {
  int sample = diff_lookup[self->step * 16 + (nibble & 15)];
  self->signal = ((sample << 8) + (self->signal * 245)) >> 8;

  int32_t max = self->output_mask - 1;
  int32_t min = -self->output_mask;
  if (self->signal > max) {
    self->signal = max;
  } else if (self->signal < min) {
    self->signal = min;
  }

  self->step += index_shift[nibble & 7];
  if (self->step > OKI_STEP_MAX) {
    self->step = OKI_STEP_MAX;
  } else if (self->step < OKI_STEP_MIN) {
    self->step = OKI_STEP_MIN;
  }

  return (int16_t)(self->signal << 4);
}

OkiAdpcm *OkiAdpcm_Create(int output_bits, ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;

  if (output_bits != OKI_OUTPUT_BITS_10 && output_bits != OKI_OUTPUT_BITS_12) {
    fprintf(stderr, "oki: unsupported output bit depth %d (expected 10 or 12)\n", output_bits);
    *error = ERR_BAD_ARGS;
    return NULL;
  }

  computeTables();

  OkiAdpcm *self = calloc(1, sizeof *self);
  if (!self) {
    *error = ERR_INTERNAL;
    return NULL;
  }
  self->output_mask = 1 << (output_bits - 1);
  OkiAdpcm_Reset(self);
  return self;
}

void OkiAdpcm_Destroy(OkiAdpcm *self) {
  if (!self) {
    return;
  }
  free(self);
}

void OkiAdpcm_Reset(OkiAdpcm *self) {
  if (!self) {
    return;
  }
  self->signal = OKI_SIGNAL_INIT;
  self->step = 0;
}

size_t OkiAdpcm_Decode(OkiAdpcm *self, const uint8_t *adpcm, size_t length, int16_t *out) {
  if (!self || !adpcm || !out) {
    return 0;
  }
  size_t count = 0;
  for (size_t i = 0; i < length; i++) {
    out[count++] = decodeNibble(self, adpcm[i] & 0x0F);
    out[count++] = decodeNibble(self, (adpcm[i] >> 4) & 0x0F);
  }
  return count;
}
