#include <pitch.h>

#include <math.h>

enum {
  YM2151_STANDARD_CLOCK = 3579545,
  YM2612_FREQ_DIVIDER = 144,
  YM2612_FREQ_SHIFT = 21,
  YM2612_FNUM_MIN = 1024,
  YM2612_FNUM_MAX = 2047,
  PITCH_A4_SEMITONE = 9,
  PITCH_REFERENCE_OCTAVE = 4
};

static const double CONCERT_A4_HZ = 440.0;

/* YM2151 note codes with low 2 bits = 0b11 (3, 7, 11, 15) are invalid slots;
 * they clamp to the nearest valid code below (its semitone). */
static const int semitone_for_note_code[16] = {
    0, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11, 11};

static long fnumForBlock(double frequency, int block, uint32_t clock_2612) {
  double scaled = frequency * YM2612_FREQ_DIVIDER *
                  pow(2.0, (double)(YM2612_FREQ_SHIFT - block)) / (double)clock_2612;
  return lround(scaled);
}

double Pitch_Frequency(uint8_t kc, uint8_t kf, uint32_t clock_2151) {
  int octave = (kc >> 4) & 7;
  int semitone = semitone_for_note_code[kc & 0x0F];
  double fraction = (double)((kf >> 2) & 0x3F) / 64.0;
  double semis_from_a4 = (octave - PITCH_REFERENCE_OCTAVE) * 12 +
                         (semitone - PITCH_A4_SEMITONE) + fraction;
  return CONCERT_A4_HZ * pow(2.0, semis_from_a4 / 12.0) *
         ((double)clock_2151 / (double)YM2151_STANDARD_CLOCK);
}

PitchResult Pitch_Convert(uint8_t kc, uint8_t kf, uint32_t clock_2151, uint32_t clock_2612) {
  double frequency = Pitch_Frequency(kc, kf, clock_2151);

  for (int block = 0; block <= 7; block++) {
    long fnum = fnumForBlock(frequency, block, clock_2612);
    if (fnum >= YM2612_FNUM_MIN && fnum <= YM2612_FNUM_MAX) {
      PitchResult result = {(uint8_t)block, (uint16_t)fnum};
      return result;
    }
  }

  long low = fnumForBlock(frequency, 0, clock_2612);
  if (low < YM2612_FNUM_MIN) {
    if (low < 0) {
      low = 0;
    }
    PitchResult result = {0, (uint16_t)low};
    return result;
  }

  long high = fnumForBlock(frequency, 7, clock_2612);
  if (high > YM2612_FNUM_MAX) {
    high = YM2612_FNUM_MAX;
  }
  PitchResult result = {7, (uint16_t)high};
  return result;
}
