#include <cest>

#include <cstdint>
#include <vector>

extern "C" {
#include <error_code.h>
#include <oki_adpcm.h>
#include <vgm_file.h>
#include <vgm_format.h>
}

static void collectOkiBlock(void *ctx, uint8_t type, const uint8_t *data, uint32_t size) {
  if (type != VGM_DATA_BLOCK_OKIM6258) {
    return;
  }
  auto *bank = (std::vector<uint8_t> *)ctx;
  bank->insert(bank->end(), data, data + size);
}

describe("OkiAdpcm", []() {
  it("decodes a known nibble sequence to the golden PCM vector", []() {
    ErrorCode err = ERR_INTERNAL;
    OkiAdpcm *d = OkiAdpcm_Create(10, &err);
    expect(d).toBeNotNull();
    expect((int)err).toBe((int)ERR_NONE);

    uint8_t adpcm[] = {0x08, 0x1F, 0x77, 0x00, 0xA5};
    int16_t out[10] = {0};
    size_t n = OkiAdpcm_Decode(d, adpcm, sizeof adpcm, out);
    expect(n).toBe((size_t)10);

    int16_t golden[10] = {-64, -32, -512, -304, 592, 2528, 2688, 2816, 5264, 3312};
    for (int i = 0; i < 10; i++) {
      expect(out[i]).toBe(golden[i]);
    }
    OkiAdpcm_Destroy(d);
  });

  it("resets state between samples (signal=-2, step=0)", []() {
    ErrorCode err = ERR_INTERNAL;
    OkiAdpcm *d = OkiAdpcm_Create(10, &err);

    uint8_t blob[] = {0x08, 0x1F, 0x77};
    int16_t first[6], carried[6], after_reset[6];

    OkiAdpcm_Decode(d, blob, sizeof blob, first);
    OkiAdpcm_Decode(d, blob, sizeof blob, carried);  // no reset: state carries over
    OkiAdpcm_Reset(d);
    OkiAdpcm_Decode(d, blob, sizeof blob, after_reset);

    bool carried_differs = false;
    bool reset_reproduces = true;
    for (int i = 0; i < 6; i++) {
      if (carried[i] != first[i]) carried_differs = true;
      if (after_reset[i] != first[i]) reset_reproduces = false;
    }
    expect(carried_differs).toBeTruthy();
    expect(reset_reproduces).toBeTruthy();
    OkiAdpcm_Destroy(d);
  });

  it("rejects an unsupported bit depth", []() {
    ErrorCode err = ERR_NONE;
    OkiAdpcm *d = OkiAdpcm_Create(8, &err);
    expect(d).toBeNull();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });

  it("accepts 12-bit output", []() {
    ErrorCode err = ERR_INTERNAL;
    OkiAdpcm *d = OkiAdpcm_Create(12, &err);
    expect(d).toBeNotNull();
    expect((int)err).toBe((int)ERR_NONE);
    OkiAdpcm_Destroy(d);
  });

  it("destroy handles NULL", []() { OkiAdpcm_Destroy(nullptr); });

  describe("against feena.vgz (real X68000 ADPCM)", []() {
    it("decodes the whole ADPCM bank to the reference PCM", []() {
      ErrorCode err = ERR_INTERNAL;
      VGMFile *f = VGMFile_Parse(FIXTURES_DIR "/feena.vgz", &err);
      expect(f).toBeNotNull();

      std::vector<uint8_t> bank;
      VGMWalker w = {};
      w.data_block = collectOkiBlock;
      expect(VGMFile_Walk(f, &w, &bank, &err)).toBeTruthy();
      expect(bank.size()).toBe((size_t)45367);

      OkiAdpcm *d = OkiAdpcm_Create(10, &err);
      std::vector<int16_t> out(bank.size() * 2);
      size_t n = OkiAdpcm_Decode(d, bank.data(), bank.size(), out.data());
      expect(n).toBe((size_t)90734);

      int16_t lo = out[0], hi = out[0];
      uint32_t hash = 2166136261u;
      for (size_t i = 0; i < n; i++) {
        if (out[i] < lo) lo = out[i];
        if (out[i] > hi) hi = out[i];
        uint16_t u = (uint16_t)out[i];
        hash = (hash ^ (u & 0xFF)) * 16777619u;
        hash = (hash ^ (u >> 8)) * 16777619u;
      }

      int16_t first10[10] = {-64, -96, -128, -160, -192, -224, -256, -288, -320, -352};
      for (int i = 0; i < 10; i++) {
        expect(out[i]).toBe(first10[i]);
      }
      expect(out[n - 1]).toBe((int16_t)-352);
      expect(lo).toBe((int16_t)-8192);
      expect(hi).toBe((int16_t)8176);
      expect(hash).toBe((uint32_t)0xCB5ECA3C);

      OkiAdpcm_Destroy(d);
      VGMFile_Destroy(f);
    });
  });
});
