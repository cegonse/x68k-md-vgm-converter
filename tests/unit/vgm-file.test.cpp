#include <cest>

#include <cstring>
#include <vector>
#include <zlib.h>

extern "C" {
#include <error_code.h>
#include <vgm_file.h>
}

using Bytes = std::vector<uint8_t>;

static void put_u32(Bytes &b, size_t off, uint32_t v) {
  b[off] = v & 0xFF;
  b[off + 1] = (v >> 8) & 0xFF;
  b[off + 2] = (v >> 16) & 0xFF;
  b[off + 3] = (v >> 24) & 0xFF;
}

static Bytes make_vgm(const Bytes &stream, uint32_t ym2151_clock, uint32_t oki_clock,
                      uint8_t oki_flags, uint32_t loop_rel, uint32_t loop_samples) {
  Bytes v(0x100, 0);
  std::memcpy(v.data(), "Vgm ", 4);
  put_u32(v, 0x08, 0x00000171);
  put_u32(v, 0x30, ym2151_clock);
  put_u32(v, 0x90, oki_clock);
  v[0x94] = oki_flags;
  put_u32(v, 0x34, 0x100 - 0x34);
  if (loop_rel) {
    put_u32(v, 0x1C, loop_rel);
    put_u32(v, 0x20, loop_samples);
  }
  put_u32(v, 0x04, (uint32_t)(0x100 + stream.size() - 4));
  v.insert(v.end(), stream.begin(), stream.end());
  return v;
}

static Bytes gzip(const Bytes &in) {
  z_stream s;
  std::memset(&s, 0, sizeof s);
  deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  Bytes out(in.size() + 128);
  s.next_in = (Bytef *)in.data();
  s.avail_in = (uInt)in.size();
  s.next_out = out.data();
  s.avail_out = (uInt)out.size();
  deflate(&s, Z_FINISH);
  out.resize(out.size() - s.avail_out);
  deflateEnd(&s);
  return out;
}

struct Capture {
  int ym = 0, oki = 0, waits = 0, blocks = 0, dac = 0, seeks = 0, ends = 0;
  uint32_t total_wait = 0;
  uint8_t last_block_type = 0;
  uint32_t last_block_size = 0;
};

static void on_ym(void *c, uint8_t, uint8_t) { ((Capture *)c)->ym++; }
static void on_oki(void *c, uint8_t, uint8_t) { ((Capture *)c)->oki++; }
static void on_wait(void *c, uint32_t s) {
  Capture *cap = (Capture *)c;
  cap->waits++;
  cap->total_wait += s;
}
static void on_block(void *c, uint8_t type, const uint8_t *, uint32_t size) {
  Capture *cap = (Capture *)c;
  cap->blocks++;
  cap->last_block_type = type;
  cap->last_block_size = size;
}
static void on_dac(void *c, uint8_t, const uint8_t *, uint32_t) { ((Capture *)c)->dac++; }
static void on_seek(void *c, uint32_t) { ((Capture *)c)->seeks++; }
static void on_end(void *c) { ((Capture *)c)->ends++; }

static VGMWalker counting_walker() {
  VGMWalker w = {};
  w.ym2151_write = on_ym;
  w.oki_write = on_oki;
  w.wait_samples = on_wait;
  w.data_block = on_block;
  w.dac_stream = on_dac;
  w.pcm_seek = on_seek;
  w.end = on_end;
  return w;
}

describe("VGMFile", []() {
  it("parses header fields (FM only)", []() {
    Bytes vgm = make_vgm({0x66}, 4000000, 0, 0, 0, 0);
    ErrorCode err = ERR_INTERNAL;
    VGMFile *f = VGMFile_ParseBytes(vgm.data(), vgm.size(), &err);
    expect(f).toBeNotNull();
    expect((int)err).toBe((int)ERR_NONE);
    expect(VGMFile_Version(f)).toBe((uint32_t)0x00000171);
    expect(VGMFile_YM2151Clock(f)).toBe((uint32_t)4000000);
    expect(VGMFile_OKIClock(f)).toBe((uint32_t)0);
    expect(VGMFile_LoopOffset(f)).toBe((uint32_t)0);
    VGMFile_Destroy(f);
  });

  it("parses OKI clock/flags and loop offset", []() {
    Bytes vgm = make_vgm({0x66}, 4000000, 4000000, 0x02, 0x40, 1234);
    ErrorCode err = ERR_INTERNAL;
    VGMFile *f = VGMFile_ParseBytes(vgm.data(), vgm.size(), &err);
    expect(f).toBeNotNull();
    expect(VGMFile_OKIClock(f)).toBe((uint32_t)4000000);
    expect(VGMFile_OKIFlags(f)).toBe((uint8_t)0x02);
    expect(VGMFile_LoopOffset(f)).toBe((uint32_t)(0x1C + 0x40));
    expect(VGMFile_LoopSamples(f)).toBe((uint32_t)1234);
    VGMFile_Destroy(f);
  });

  it("rejects bad magic", []() {
    Bytes bad(0x100, 0);
    std::memcpy(bad.data(), "NOPE", 4);
    ErrorCode err = ERR_NONE;
    VGMFile *f = VGMFile_ParseBytes(bad.data(), bad.size(), &err);
    expect(f).toBeNull();
    expect((int)err).toBe((int)ERR_PARSE);
  });

  it("decompresses gzipped input transparently", []() {
    Bytes vgm = make_vgm({0x66}, 4000000, 0, 0, 0, 0);
    Bytes gz = gzip(vgm);
    expect(gz[0]).toBe((uint8_t)0x1F);
    expect(gz[1]).toBe((uint8_t)0x8B);
    ErrorCode err = ERR_INTERNAL;
    VGMFile *f = VGMFile_ParseBytes(gz.data(), gz.size(), &err);
    expect(f).toBeNotNull();
    expect(VGMFile_YM2151Clock(f)).toBe((uint32_t)4000000);
    VGMFile_Destroy(f);
  });

  it("walks every command length class and lands on the end marker", []() {
    Bytes stream = {
      0x54, 0x20, 0x0A,                    // YM2151 write
      0xB7, 0x00, 0x05,                    // OKIM6258 write
      0x61, 0x10, 0x00,                    // wait 16
      0x62,                                // wait 735
      0x63,                                // wait 882
      0x75,                                // wait 6
      0x50, 0xAA,                          // skip 1
      0x51, 0x01, 0x02,                    // skip 2
      0x4F, 0x00,                          // skip 1
      0x40, 0x01, 0x02,                    // skip 2
      0x30, 0x01,                          // skip 1
      0xA0, 0x01, 0x02,                    // skip 2
      0xB0, 0x01, 0x02,                    // skip 2
      0xC0, 0x01, 0x02, 0x03,              // skip 3
      0xD0, 0x01, 0x02, 0x03,              // skip 3
      0xE1, 0x01, 0x02, 0x03, 0x04,        // skip 4
      0x90, 0x00, 0x00, 0x00, 0x00,        // DAC stream setup
      0x92, 0x00, 0x44, 0xAC, 0x00, 0x00,  // DAC stream freq
      0x94, 0xFF,                          // DAC stream stop
      0xE0, 0x00, 0x00, 0x00, 0x00,        // PCM seek
      0x67, 0x66, 0x04, 0x03, 0x00, 0x00, 0x00, 0xAA, 0xBB, 0xCC, // data block
      0x66,                                // end
    };
    Bytes vgm = make_vgm(stream, 4000000, 4000000, 0, 0, 0);
    ErrorCode err = ERR_INTERNAL;
    VGMFile *f = VGMFile_ParseBytes(vgm.data(), vgm.size(), &err);
    expect(f).toBeNotNull();

    Capture cap;
    VGMWalker w = counting_walker();
    bool ok = VGMFile_Walk(f, &w, &cap, &err);

    expect(ok).toBeTruthy();
    expect((int)err).toBe((int)ERR_NONE);
    expect(cap.ends).toBe(1);
    expect(cap.ym).toBe(1);
    expect(cap.oki).toBe(1);
    expect(cap.waits).toBe(4);
    expect(cap.total_wait).toBe((uint32_t)(16 + 735 + 882 + 6));
    expect(cap.dac).toBe(3);
    expect(cap.seeks).toBe(1);
    expect(cap.blocks).toBe(1);
    expect(cap.last_block_type).toBe((uint8_t)0x04);
    expect(cap.last_block_size).toBe((uint32_t)3);
    VGMFile_Destroy(f);
  });

  it("halts on an undefined command", []() {
    Bytes vgm = make_vgm({0x00}, 4000000, 0, 0, 0, 0);
    ErrorCode err = ERR_NONE;
    VGMFile *f = VGMFile_ParseBytes(vgm.data(), vgm.size(), &err);
    expect(f).toBeNotNull();
    Capture cap;
    VGMWalker w = counting_walker();
    bool ok = VGMFile_Walk(f, &w, &cap, &err);
    expect(ok).toBeFalsy();
    expect((int)err).toBe((int)ERR_PARSE);
    expect(cap.ends).toBe(0);
    VGMFile_Destroy(f);
  });

  it("halts on a truncated command", []() {
    Bytes vgm = make_vgm({0x54, 0x20}, 4000000, 0, 0, 0, 0);
    ErrorCode err = ERR_NONE;
    VGMFile *f = VGMFile_ParseBytes(vgm.data(), vgm.size(), &err);
    expect(f).toBeNotNull();
    Capture cap;
    VGMWalker w = counting_walker();
    bool ok = VGMFile_Walk(f, &w, &cap, &err);
    expect(ok).toBeFalsy();
    expect((int)err).toBe((int)ERR_PARSE);
    VGMFile_Destroy(f);
  });

  it("destroy handles NULL", []() { VGMFile_Destroy(nullptr); });

  describe("against feena.vgz (real X68000 source)", []() {
    it("parses the gzipped header fields", []() {
      ErrorCode err = ERR_INTERNAL;
      VGMFile *f = VGMFile_Parse(FIXTURES_DIR "/feena.vgz", &err);
      expect(f).toBeNotNull();
      expect((int)err).toBe((int)ERR_NONE);
      expect(VGMFile_Version(f)).toBe((uint32_t)0x00000161);
      expect(VGMFile_YM2151Clock(f)).toBe((uint32_t)4000000);
      expect(VGMFile_OKIClock(f)).toBe((uint32_t)8000000);
      expect(VGMFile_OKIFlags(f)).toBe((uint8_t)0x06);
      expect(VGMFile_Rate(f)).toBe((uint32_t)0);
      expect(VGMFile_LoopOffset(f)).toBe((uint32_t)(0x1C + 0x15511));
      expect(VGMFile_LoopSamples(f)).toBe((uint32_t)3374856);
      expect(VGMFile_TotalSamples(f)).toBe((uint32_t)4484386);
      VGMFile_Destroy(f);
    });

    it("walks the whole command stream cleanly to the end marker", []() {
      ErrorCode err = ERR_INTERNAL;
      VGMFile *f = VGMFile_Parse(FIXTURES_DIR "/feena.vgz", &err);
      expect(f).toBeNotNull();

      Capture cap;
      VGMWalker w = counting_walker();
      bool ok = VGMFile_Walk(f, &w, &cap, &err);

      expect(ok).toBeTruthy();
      expect((int)err).toBe((int)ERR_NONE);
      expect(cap.ends).toBe(1);
      expect(cap.ym).toBe(39257);
      expect(cap.oki).toBe(50);
      expect(cap.waits).toBe(30442);
      expect(cap.blocks).toBe(5);
      expect(cap.dac).toBe(49);
      expect(cap.seeks).toBe(0);
      expect(cap.total_wait).toBe((uint32_t)4484386);
      expect(cap.total_wait).toBe(VGMFile_TotalSamples(f));
      VGMFile_Destroy(f);
    });
  });
});
