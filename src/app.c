#include <app_main.h>

#include <args.h>
#include <channel_map.h>
#include <error_code.h>
#include <fm_transcribe.h>
#include <pcm_stream.h>
#include <stdio.h>
#include <string.h>
#include <vgm_file.h>
#include <vgm_writer.h>
#include <ym2612.h>

typedef struct WalkContext {
  FmTranscriber *fm;
  PcmStream *pcm;
  VGMWriter *writer;
} WalkContext;

static void onYm2151(void *ctx, uint8_t reg, uint8_t value) {
  FmTranscriber_Write(((WalkContext *)ctx)->fm, reg, value);
}
static void onOki(void *ctx, uint8_t reg, uint8_t value) {
  PcmStream_OkiWrite(((WalkContext *)ctx)->pcm, reg, value);
}
static void onWait(void *ctx, uint32_t samples) {
  VGMWriter_WaitSamples(((WalkContext *)ctx)->writer, samples);
}
static void onDataBlock(void *ctx, uint8_t type, const uint8_t *data, uint32_t size) {
  PcmStream_AddDataBlock(((WalkContext *)ctx)->pcm, type, data, size);
}
static void onDacStream(void *ctx, uint8_t command, const uint8_t *operands, uint32_t length) {
  PcmStream_DacStream(((WalkContext *)ctx)->pcm, command, operands, length);
}
static void onSeek(void *ctx, uint32_t offset) {
  PcmStream_Seek(((WalkContext *)ctx)->pcm, offset);
}
static void onLoop(void *ctx) {
  VGMWriter_MarkLoop(((WalkContext *)ctx)->writer);
}
static void onEnd(void *ctx) {
  VGMWriter_End(((WalkContext *)ctx)->writer);
}

static void selectKeepList(const Args *args, bool has_pcm, uint8_t *keep, int *count) {
  if (args->fm_channel_count < 0) {
    *count = has_pcm ? 5 : 6;
    for (int i = 0; i < *count; i++) {
      keep[i] = (uint8_t)i;
    }
    return;
  }
  *count = args->fm_channel_count;
  for (int i = 0; i < *count; i++) {
    keep[i] = args->fm_channels[i];
  }
}

static void printDropSummary(FmDropStats drops, bool has_pcm, int keep_count) {
  fprintf(stderr, "transcribed: %d FM channel(s) kept, %d dropped%s\n", keep_count,
          drops.dropped_channels, has_pcm ? "; PCM -> YM2612 ch6 (DAC)" : "");
  if (drops.noise_present) {
    fprintf(stderr, "  lossy drop: YM2151 noise (no YM2612 equivalent)\n");
  }
  if (drops.dt2_present) {
    fprintf(stderr, "  lossy drop: YM2151 DT2 detune (no YM2612 equivalent)\n");
  }
}

int App_Run(int argc, char **argv) {
  Args args;
  ErrorCode err;
  if (!Args_Parse(argc, argv, &args, &err)) {
    return err;
  }

  VGMFile *vgm = VGMFile_Parse(args.input, &err);
  if (!vgm) {
    return err;
  }

  uint32_t clock_2151 = VGMFile_YM2151Clock(vgm);
  if (clock_2151 == 0) {
    fprintf(stderr, "%s: not an X68000 VGM (no YM2151 clock)\n", args.input);
    VGMFile_Destroy(vgm);
    return ERR_UNSUPPORTED;
  }

  bool has_pcm = VGMFile_OKIClock(vgm) != 0;
  uint32_t rate = VGMFile_Rate(vgm);
  uint32_t clock_2612 = (rate == 50) ? YM2612_CLOCK_PAL : YM2612_CLOCK_NTSC;

  uint8_t keep[8];
  int keep_count;
  selectKeepList(&args, has_pcm, keep, &keep_count);

  ChannelMap map;
  if (!ChannelMap_Build(keep, keep_count, has_pcm, &map, &err)) {
    VGMFile_Destroy(vgm);
    return err;
  }

  VGMWriter *writer = VGMWriter_Create(clock_2612, rate, &err);
  FmTranscriber *fm = writer ? FmTranscriber_Create(&map, clock_2151, clock_2612, writer, &err) : NULL;
  PcmStream *pcm = fm ? PcmStream_Create(VGMFile_OKIClock(vgm), VGMFile_OKIFlags(vgm), writer, &err) : NULL;
  if (!writer || !fm || !pcm) {
    PcmStream_Destroy(pcm);
    FmTranscriber_Destroy(fm);
    VGMWriter_Destroy(writer);
    VGMFile_Destroy(vgm);
    return err;
  }

  WalkContext ctx = {fm, pcm, writer};
  VGMWalker walker;
  memset(&walker, 0, sizeof walker);
  walker.ym2151_write = onYm2151;
  walker.oki_write = onOki;
  walker.wait_samples = onWait;
  walker.data_block = onDataBlock;
  walker.dac_stream = onDacStream;
  walker.pcm_seek = onSeek;
  walker.loop_point = onLoop;
  walker.end = onEnd;

  int result = ERR_NONE;
  if (!VGMFile_Walk(vgm, &walker, &ctx, &err)) {
    result = err;
  } else if (!VGMWriter_Save(writer, args.output, &err)) {
    result = err;
  } else {
    printDropSummary(FmTranscriber_Drops(fm), has_pcm, keep_count);
  }

  PcmStream_Destroy(pcm);
  FmTranscriber_Destroy(fm);
  VGMWriter_Destroy(writer);
  VGMFile_Destroy(vgm);
  return result;
}
