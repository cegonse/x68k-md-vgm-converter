#include <cest>

#include <cstdint>

extern "C" {
#include <pitch.h>
}

static const uint32_t C2151 = 4000000;
static const uint32_t C2612 = 7670442;

struct Ref {
  const char *name;
  uint8_t kc;
  double freq;
  uint8_t block;
  uint16_t fnum;
};

describe("Pitch", []() {
  it("matches the documented reference values", []() {
    cest::withParameter<Ref>()
        .withValue(Ref{"A4", 0x4C, 491.68, 4, 1210})
        .withValue(Ref{"C4", 0x40, 292.36, 3, 1439})
        .withValue(Ref{"C5", 0x50, 584.71, 4, 1439})
        .withValue(Ref{"C2", 0x20, 73.09, 1, 1439})
        .withValue(Ref{"B6", 0x6E, 2207.58, 6, 1358})
        .thenDo([](Ref r) {
          expect(Pitch_Frequency(r.kc, 0, C2151)).toBe(r.freq, 0.01);
          PitchResult p = Pitch_Convert(r.kc, 0, C2151, C2612);
          expect(p.block).toBe(r.block);
          expect(p.fnum).toBe(r.fnum);
        });
  });

  it("keeps fnum and increments block when the octave doubles", []() {
    PitchResult c4 = Pitch_Convert(0x40, 0, C2151, C2612);
    PitchResult c5 = Pitch_Convert(0x50, 0, C2151, C2612);
    expect(c5.fnum).toBe(c4.fnum);
    expect(c5.block).toBe((uint8_t)(c4.block + 1));
  });

  it("moves pitch up monotonically with the key fraction", []() {
    double f0 = Pitch_Frequency(0x4C, 0x00, C2151);
    double f_mid = Pitch_Frequency(0x4C, 0x80, C2151);
    double f_hi = Pitch_Frequency(0x4C, 0xFC, C2151);
    expect(f_mid).toBeGreaterThan(f0);
    expect(f_hi).toBeGreaterThan(f_mid);
  });

  it("preserves the X68000 4 MHz tuning shift (A4 well above 440)", []() {
    expect(Pitch_Frequency(0x4C, 0, C2151)).toBeGreaterThan(490.0);
  });

  it("clamps invalid note codes to the nearest valid code", []() {
    // note code 3 is invalid -> clamps to code 2 (semitone D)
    expect(Pitch_Frequency(0x43, 0, C2151)).toBe(Pitch_Frequency(0x42, 0, C2151), 1e-9);
  });
});
