#include <cest>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <vector>

extern "C" {
#include <app_main.h>
#include <error_code.h>
#include <vgm_file.h>
}

static const char *MD_VGM = "/tmp/x68k_feena_md.vgm";
static const char *XGM_OUT = "/tmp/x68k_feena.xgm";

struct Counts {
  int datablocks = 0, dac = 0, ends = 0;
};
static void onBlock(void *c, uint8_t, const uint8_t *, uint32_t) { ((Counts *)c)->datablocks++; }
static void onDac(void *c, uint8_t, const uint8_t *, uint32_t) { ((Counts *)c)->dac++; }
static void onEnd(void *c) { ((Counts *)c)->ends++; }

static int runApp(const char *input, const char *output, const char *channels) {
  std::vector<char> in(input, input + strlen(input) + 1);
  std::vector<char> out(output, output + strlen(output) + 1);
  std::vector<char> ch(channels, channels + strlen(channels) + 1);
  char a0[] = "x68k-md-vgm-conv";
  char flag[] = "--fm-channels";
  char *argv[] = {a0, in.data(), out.data(), flag, ch.data()};
  return App_Run(5, argv);
}

describe("Full pipeline: feena.vgz -> MD VGM -> xgmtool", []() {
  it("App_Run transcribes feena to a Mega Drive VGM", []() {
    int rc = runApp(FIXTURES_DIR "/feena.vgz", MD_VGM, "0,1,2,3,4");
    expect(rc).toBe((int)ERR_NONE);

    ErrorCode err = ERR_INTERNAL;
    VGMFile *f = VGMFile_Parse(MD_VGM, &err);
    expect(f).toBeNotNull();

    // Target-side header: YM2151 and OKIM6258 clocks zeroed, v1.61, length carried.
    expect(VGMFile_YM2151Clock(f)).toBe((uint32_t)0);
    expect(VGMFile_OKIClock(f)).toBe((uint32_t)0);
    expect(VGMFile_Version(f)).toBe((uint32_t)0x00000161);
    expect(VGMFile_Rate(f)).toBe((uint32_t)60);  // explicit NTSC (X68000 is 60 Hz)
    expect(VGMFile_TotalSamples(f)).toBe((uint32_t)4484386);
    expect(VGMFile_LoopSamples(f)).toBe((uint32_t)3374856);

    Counts k;
    VGMWalker w = {};
    w.data_block = onBlock;
    w.dac_stream = onDac;
    w.end = onEnd;
    expect(VGMFile_Walk(f, &w, &k, &err)).toBeTruthy();
    expect(k.ends).toBe(1);
    expect(k.datablocks).toBe(5);  // one decoded type-0x00 block per source ADPCM block
    expect(k.dac).toBe(49);         // DAC-stream commands retargeted 1:1
    VGMFile_Destroy(f);
  });

  it("xgmtool converts the Mega Drive VGM to a valid XGM", []() {
    std::string cmd = std::string(XGMTOOL_BIN) + " " + MD_VGM + " " + XGM_OUT + " -s";
    int rc = system(cmd.c_str());
    expect(WIFEXITED(rc)).toBeTruthy();
    expect(WEXITSTATUS(rc)).toBe(0);

    FILE *f = fopen(XGM_OUT, "rb");
    expect(f != nullptr).toBeTruthy();
    if (f) {
      fseek(f, 0, SEEK_END);
      long size = ftell(f);
      fclose(f);
      expect(size > 0).toBeTruthy();
    }
  });
});
