#include <cest>

#include <cstring>

extern "C" {
#include <error_code.h>
#include <vgm_writer.h>
}

static uint32_t u32(const uint8_t *b, size_t off) {
  return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
         ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

describe("VGMWriter", []() {
  it("emits a valid Mega Drive VGM header", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 60, &err);
    expect(w).toBeNotNull();
    expect((int)err).toBe((int)ERR_NONE);

    VGMWriter_WriteYM2612(w, 0, 0x28, 0xF0);
    VGMWriter_WriteYM2612(w, 1, 0xA4, 0x22);
    VGMWriter_WaitSamples(w, 735);
    VGMWriter_End(w);

    size_t n = 0;
    const uint8_t *b = VGMWriter_Bytes(w, &n);
    expect(b).toBeNotNull();
    expect(std::memcmp(b, "Vgm ", 4) == 0).toBeTruthy();
    expect(u32(b, 0x08)).toBe((uint32_t)0x00000151);
    expect(u32(b, 0x2C)).toBe((uint32_t)7670442);   // YM2612 clock
    expect(u32(b, 0x30)).toBe((uint32_t)0);          // YM2151 clock zeroed
    expect(u32(b, 0x0C)).toBe((uint32_t)0);          // SN76489 clock zeroed
    expect(u32(b, 0x90)).toBe((uint32_t)0);          // OKIM6258 clock zeroed
    expect(u32(b, 0x24)).toBe((uint32_t)60);         // rate
    expect(u32(b, 0x18)).toBe((uint32_t)735);        // total samples
    expect(u32(b, 0x34)).toBe((uint32_t)(0x100 - 0x34)); // data offset
    expect(u32(b, 0x04)).toBe((uint32_t)(n - 4));    // EOF offset

    expect(b[0x100]).toBe((uint8_t)0x52);            // port 0 write
    expect(b[0x103]).toBe((uint8_t)0x53);            // port 1 write
    VGMWriter_Destroy(w);
  });

  it("records loop point and loop samples", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    VGMWriter_WaitSamples(w, 100);   // 3 stream bytes before the loop mark
    VGMWriter_MarkLoop(w);
    VGMWriter_WaitSamples(w, 200);
    VGMWriter_End(w);

    size_t n = 0;
    const uint8_t *b = VGMWriter_Bytes(w, &n);
    expect(b).toBeNotNull();
    uint32_t loop_abs = 0x100 + 3;
    expect(u32(b, 0x1C)).toBe(loop_abs - 0x1C);
    expect(u32(b, 0x20)).toBe((uint32_t)200);
    VGMWriter_Destroy(w);
  });

  it("splits waits larger than 65535 samples", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    VGMWriter_WaitSamples(w, 70000);
    VGMWriter_End(w);
    size_t n = 0;
    const uint8_t *b = VGMWriter_Bytes(w, &n);
    expect(u32(b, 0x18)).toBe((uint32_t)70000);
    expect(b[0x100]).toBe((uint8_t)0x61);
    VGMWriter_Destroy(w);
  });

  it("destroy handles NULL", []() { VGMWriter_Destroy(nullptr); });
});
