#include <cest>

#include <cstdint>
#include <vector>

extern "C" {
#include <error_code.h>
#include <pcm_stream.h>
#include <vgm_writer.h>
}

static std::vector<uint8_t> streamOf(VGMWriter *w) {
  size_t n = 0;
  const uint8_t *b = VGMWriter_Bytes(w, &n);
  return std::vector<uint8_t>(b + 0x100, b + n);
}

static bool contains(const std::vector<uint8_t> &hay, const std::vector<uint8_t> &needle) {
  if (needle.empty() || hay.size() < needle.size()) return false;
  for (size_t i = 0; i + needle.size() <= hay.size(); i++) {
    bool match = true;
    for (size_t j = 0; j < needle.size(); j++) {
      if (hay[i + j] != needle[j]) { match = false; break; }
    }
    if (match) return true;
  }
  return false;
}

describe("PcmStream", []() {
  it("computes the native sample rate (feena: 8 MHz, divider 512)", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    PcmStream *p = PcmStream_Create(8000000, 0x06, w, &err);
    expect(p).toBeNotNull();
    expect(PcmStream_SampleRate(p)).toBe((uint32_t)7813);
    PcmStream_Destroy(p);
    VGMWriter_Destroy(w);
  });

  it("emits DAC enable then a decoded 8-bit type-0x00 data block", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    PcmStream *p = PcmStream_Create(8000000, 0x06, w, &err);
    expect(PcmStream_HasPcm(p)).toBeFalsy();

    uint8_t adpcm[] = {0x08, 0x1F};  // decodes to int16 {-64,-32,-512,-304}
    PcmStream_AddDataBlock(p, 0x04, adpcm, sizeof adpcm);
    expect(PcmStream_HasPcm(p)).toBeTruthy();

    auto s = streamOf(w);
    // DAC enable: 0x52 0x2B 0x80
    expect(contains(s, {0x52, 0x2B, 0x80})).toBeTruthy();
    // data block: 67 66 00 <size=4 LE> then to8bit values 7F 7F 78 7B
    expect(contains(s, {0x67, 0x66, 0x00, 0x04, 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x78, 0x7B})).toBeTruthy();
    PcmStream_Destroy(p);
    VGMWriter_Destroy(w);
  });

  it("ignores non-OKIM6258 data blocks", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    PcmStream *p = PcmStream_Create(8000000, 0x06, w, &err);
    uint8_t data[] = {1, 2, 3};
    PcmStream_AddDataBlock(p, 0x00, data, sizeof data);
    expect(streamOf(w).size()).toBe((size_t)0);
    expect(PcmStream_HasPcm(p)).toBeFalsy();
    PcmStream_Destroy(p);
    VGMWriter_Destroy(w);
  });

  it("retargets the DAC-stream setup to YM2612 (02 00 2A)", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    PcmStream *p = PcmStream_Create(8000000, 0x06, w, &err);
    uint8_t setup[] = {0x00, 0x17, 0x00, 0x01};  // OKIM6258 stream setup
    PcmStream_DacStream(p, 0x90, setup, 4);
    expect(contains(streamOf(w), {0x90, 0x00, 0x02, 0x00, 0x2A})).toBeTruthy();
    PcmStream_Destroy(p);
    VGMWriter_Destroy(w);
  });

  it("retargets stream data to bank 0x00", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    PcmStream *p = PcmStream_Create(8000000, 0x06, w, &err);
    uint8_t data[] = {0x00, 0x04, 0x01, 0x00};  // source bank id 0x04
    PcmStream_DacStream(p, 0x91, data, 4);
    expect(contains(streamOf(w), {0x91, 0x00, 0x00, 0x01, 0x00})).toBeTruthy();
    PcmStream_Destroy(p);
    VGMWriter_Destroy(w);
  });

  it("sets the DAC-stream frequency to the decoded-PCM playback rate", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    PcmStream *p = PcmStream_Create(8000000, 0x06, w, &err);  // 8 MHz, divider 512
    // source stream freq is the OKI data rate; our decoded PCM must play at
    // clock/divider = 8000000/512 = 15625 = 0x3D09 (2x the 7813 data rate).
    uint8_t freq[] = {0x00, 0x00, 0x00, 0x00, 0x00};  // source freq bytes ignored
    PcmStream_DacStream(p, 0x92, freq, 5);
    expect(contains(streamOf(w), {0x92, 0x00, 0x09, 0x3D, 0x00, 0x00})).toBeTruthy();
    PcmStream_Destroy(p);
    VGMWriter_Destroy(w);
  });

  it("scales long-start offset and length by 2 (ADPCM -> PCM doubling)", []() {
    ErrorCode err = ERR_INTERNAL;
    VGMWriter *w = VGMWriter_Create(7670442, 0, &err);
    PcmStream *p = PcmStream_Create(8000000, 0x06, w, &err);
    // ss=0, addr=0x10, mode=1, len=0x20
    uint8_t start[] = {0x00, 0x10, 0, 0, 0, 0x01, 0x20, 0, 0, 0};
    PcmStream_DacStream(p, 0x93, start, 10);
    // -> ss=0, addr=0x20, mode=1, len=0x40
    expect(contains(streamOf(w), {0x93, 0x00, 0x20, 0, 0, 0, 0x01, 0x40, 0, 0, 0})).toBeTruthy();
    PcmStream_Destroy(p);
    VGMWriter_Destroy(w);
  });

  it("destroy handles NULL", []() { PcmStream_Destroy(nullptr); });
});
