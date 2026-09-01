#pragma once

#include <stdint.h>

typedef struct PitchResult {
  uint8_t block;
  uint16_t fnum;
} PitchResult;

double Pitch_Frequency(uint8_t kc, uint8_t kf, uint32_t clock_2151);
PitchResult Pitch_Convert(uint8_t kc, uint8_t kf, uint32_t clock_2151, uint32_t clock_2612);
