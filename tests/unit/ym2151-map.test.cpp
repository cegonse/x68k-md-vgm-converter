#include <cest>

#include <cstdint>
#include <vector>

extern "C" {
#include <channel_map.h>
#include <error_code.h>
#include <fm_transcribe.h>
#include <vgm_writer.h>
}

static const uint32_t C2151 = 4000000;
static const uint32_t C2612 = 7670442;

struct Write {
  uint8_t cmd, reg, val;
};

static std::vector<Write> collectWrites(VGMWriter *w) {
  size_t n = 0;
  const uint8_t *b = VGMWriter_Bytes(w, &n);
  std::vector<Write> out;
  for (size_t i = 0x100; i + 3 <= n; i += 3) {
    out.push_back(Write{b[i], b[i + 1], b[i + 2]});
  }
  return out;
}

static bool hasWrite(const std::vector<Write> &v, uint8_t cmd, uint8_t reg, uint8_t val) {
  for (auto &w : v) {
    if (w.cmd == cmd && w.reg == reg && w.val == val) return true;
  }
  return false;
}

static ChannelMap mapOf(std::vector<uint8_t> keep, bool pcm) {
  ChannelMap m;
  ErrorCode err;
  ChannelMap_Build(keep.data(), (int)keep.size(), pcm, &m, &err);
  return m;
}

describe("FmTranscriber", []() {
  it("copies operator fields with addressing translation", []() {
    ChannelMap m = mapOf({0}, false);
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 60, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);
    expect(t).toBeNotNull();

    // channel 0, slot 0 total level (YM2151 0x60) -> YM2612 0x40
    FmTranscriber_Write(t, 0x60, 0x22);
    // channel 0, slot 2 total level: op = 0 + 8*2 = 0x10 -> reg 0x70 -> YM2612 0x48
    FmTranscriber_Write(t, 0x70, 0x33);
    // channel 0, slot 0 DT1/MUL (0x40) -> YM2612 0x30
    FmTranscriber_Write(t, 0x40, 0x1A);

    auto writes = collectWrites(w);
    expect(hasWrite(writes, 0x52, 0x40, 0x22)).toBeTruthy();
    expect(hasWrite(writes, 0x52, 0x48, 0x33)).toBeTruthy();
    expect(hasWrite(writes, 0x52, 0x30, 0x1A)).toBeTruthy();
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("routes port-1 channels to command 0x53", []() {
    ChannelMap m = mapOf({0, 1, 2, 3}, false);  // channel 3 -> YM2612 ch3 (port 1)
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 0, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);

    // channel 3, slot 0 total level: op = 3 -> reg 0x63 -> YM2612 0x40 + 0 + 0 on port 1
    FmTranscriber_Write(t, 0x63, 0x40);
    auto writes = collectWrites(w);
    expect(hasWrite(writes, 0x53, 0x40, 0x40)).toBeTruthy();
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("repacks L/R (bit-swapped) and PMS/AMS into 0xB4", []() {
    ChannelMap m = mapOf({0}, false);
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 0, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);

    // 0x20: R=1(bit7), L=0(bit6), FB=5, ALG=3  -> value 0x80|0x28|0x03 = 0xAB
    FmTranscriber_Write(t, 0x20, 0xAB);
    // 0x38: PMS=3 (bits6-4), AMS=2 (bits1-0) -> 0x32
    FmTranscriber_Write(t, 0x38, 0x32);

    auto writes = collectWrites(w);
    expect(hasWrite(writes, 0x52, 0xB0, 0x2B)).toBeTruthy();  // FB5/ALG3

    // last 0xB4 write: R-only source -> YM2612 R=bit6 set, L=bit7 clear; ams=2->bits5-4, pms=3->bits2-0
    uint8_t b4 = 0;
    for (auto &x : writes) {
      if (x.cmd == 0x52 && x.reg == 0xB4) b4 = x.val;
    }
    expect(b4 & 0x80).toBe(0);           // Left disabled
    expect((b4 >> 6) & 1).toBe(1);       // Right enabled
    expect((b4 >> 4) & 0x03).toBe(2);    // AMS
    expect(b4 & 0x07).toBe(3);           // PMS
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("re-encodes key on/off and never emits channel select 3 or 7", []() {
    ChannelMap m = mapOf({0, 1, 2, 3}, false);
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 0, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);

    // key channel 0, all four operators (bits 6-3 = 0x78)
    FmTranscriber_Write(t, 0x08, 0x78);
    // key channel 3 (-> YM2612 ch3 -> select 4), all operators
    FmTranscriber_Write(t, 0x08, 0x78 | 3);

    auto writes = collectWrites(w);
    expect(hasWrite(writes, 0x52, 0x28, 0xF0)).toBeTruthy();  // ch0: opmask F0 | select 0
    expect(hasWrite(writes, 0x52, 0x28, 0xF4)).toBeTruthy();  // ch3: opmask F0 | select 4
    for (auto &x : writes) {
      if (x.reg == 0x28) {
        expect((x.val & 0x07)).Not->toBe(3);
        expect((x.val & 0x07)).Not->toBe(7);
      }
    }
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("computes pitch (KC/KF -> fnum/block), high write before low", []() {
    ChannelMap m = mapOf({0}, false);
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 0, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);

    FmTranscriber_Write(t, 0x28, 0x4C);  // KC for A4
    auto writes = collectWrites(w);
    expect(writes.size()).toBe((size_t)2);
    expect(writes[0].cmd).toBe((uint8_t)0x52);
    expect(writes[0].reg).toBe((uint8_t)0xA4);  // block+fnum high FIRST
    expect(writes[0].val).toBe((uint8_t)0x24);  // block 4, fnum 1210 high
    expect(writes[1].reg).toBe((uint8_t)0xA0);  // fnum low commits
    expect(writes[1].val).toBe((uint8_t)0xBA);
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("drops writes to channels not in the keep-list", []() {
    ChannelMap m = mapOf({0}, false);
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 0, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);

    FmTranscriber_Write(t, 0x61, 0x22);  // channel 1 (op1) total level: dropped
    FmTranscriber_Write(t, 0x08, 0x78 | 1);  // key channel 1: dropped
    expect(collectWrites(w).size()).toBe((size_t)0);
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("drops DT2 but copies D2R, and flags the drop", []() {
    ChannelMap m = mapOf({0}, false);
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 0, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);

    // 0xC0: DT2=3 (bits7-6), D2R=0x15 -> value 0xD5
    FmTranscriber_Write(t, 0xC0, 0xD5);
    expect(hasWrite(collectWrites(w), 0x52, 0x70, 0x15)).toBeTruthy();
    expect(FmTranscriber_Drops(t).dt2_present).toBeTruthy();
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("flags noise and produces no output for it", []() {
    ChannelMap m = mapOf({0}, false);
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 0, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);
    FmTranscriber_Write(t, 0x0F, 0x90);  // noise enable
    expect(collectWrites(w).size()).toBe((size_t)0);
    expect(FmTranscriber_Drops(t).noise_present).toBeTruthy();
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("reports the number of dropped channels", []() {
    ChannelMap m = mapOf({0, 1}, false);
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(C2612, 0, &err);
    FmTranscriber *t = FmTranscriber_Create(&m, C2151, C2612, w, &err);
    expect(FmTranscriber_Drops(t).dropped_channels).toBe(6);
    FmTranscriber_Destroy(t);
    VGMWriter_Destroy(w);
  });

  it("destroy handles NULL", []() { FmTranscriber_Destroy(nullptr); });
});
