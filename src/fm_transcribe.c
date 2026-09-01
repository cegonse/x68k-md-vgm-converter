#include <fm_transcribe.h>

#include <pitch.h>
#include <stdlib.h>
#include <ym2151.h>
#include <ym2612.h>

enum { YM2612_LR_BOTH = 0xC0, YM2612_PORT0_CHANNELS = 3 };

struct FmTranscriber {
  ChannelMap map;
  uint32_t clock_2151;
  uint32_t clock_2612;
  VGMWriter *writer;
  uint8_t key_code[8];
  uint8_t key_fraction[8];
  uint8_t lr[8];
  uint8_t pms_ams[8];
  FmDropStats drops;
};

static int channelInPort(int target) {
  return target < YM2612_PORT0_CHANNELS ? target : target - YM2612_PORT0_CHANNELS;
}

static void writeLrPmsAms(FmTranscriber *self, int src, int target) {
  uint8_t combined = (uint8_t)(self->lr[src] | self->pms_ams[src]);
  VGMWriter_WriteYM2612(self->writer, (uint8_t)self->map.port[src],
                        (uint8_t)(YM2612_REG_LR_AMS_PMS + channelInPort(target)), combined);
}

static void writeRlFbConn(FmTranscriber *self, int src, int target, uint8_t value) {
  uint8_t alg_fb = value & 0x3F;
  VGMWriter_WriteYM2612(self->writer, (uint8_t)self->map.port[src],
                        (uint8_t)(YM2612_REG_ALGORITHM_FEEDBACK + channelInPort(target)), alg_fb);

  uint8_t left = (value >> 6) & 1;
  uint8_t right = (value >> 7) & 1;
  uint8_t lr = (uint8_t)((left << 7) | (right << 6));
  self->lr[src] = lr ? lr : YM2612_LR_BOTH;
  writeLrPmsAms(self, src, target);
}

static void writePmsAms(FmTranscriber *self, int src, int target, uint8_t value) {
  uint8_t pms = (value >> 4) & 0x07;
  uint8_t ams = value & 0x03;
  self->pms_ams[src] = (uint8_t)((ams << 4) | pms);
  writeLrPmsAms(self, src, target);
}

static void writePitch(FmTranscriber *self, int src, int target) {
  PitchResult p = Pitch_Convert(self->key_code[src], self->key_fraction[src],
                                self->clock_2151, self->clock_2612);
  uint8_t port = (uint8_t)self->map.port[src];
  uint8_t offset = (uint8_t)channelInPort(target);
  uint8_t high = (uint8_t)((p.block << 3) | ((p.fnum >> 8) & 0x07));
  uint8_t low = (uint8_t)(p.fnum & 0xFF);
  VGMWriter_WriteYM2612(self->writer, port, (uint8_t)(YM2612_REG_BLOCK_FNUM_HIGH + offset), high);
  VGMWriter_WriteYM2612(self->writer, port, (uint8_t)(YM2612_REG_FNUM_LOW + offset), low);
}

static void writeChannelGroup(FmTranscriber *self, uint8_t reg, uint8_t value) {
  int src = reg & 7;
  int target = self->map.target[src];
  if (target < 0) {
    return;
  }
  switch (reg & 0x18) {
    case 0x00:
      writeRlFbConn(self, src, target, value);
      break;
    case 0x08:
      self->key_code[src] = value;
      writePitch(self, src, target);
      break;
    case 0x10:
      self->key_fraction[src] = value;
      writePitch(self, src, target);
      break;
    default:
      writePmsAms(self, src, target, value);
      break;
  }
}

static void writeOperatorGroup(FmTranscriber *self, uint8_t reg, uint8_t value) {
  int op = reg & 0x1F;
  int src = op & 7;
  int slot = (op >> 3) & 3;
  int target = self->map.target[src];
  if (target < 0) {
    return;
  }

  uint8_t base;
  uint8_t out_value = value;
  switch (reg & 0xE0) {
    case YM2151_REG_DT1_MUL:
      base = YM2612_REG_DETUNE_MUL;
      break;
    case YM2151_REG_TOTAL_LEVEL:
      base = YM2612_REG_TOTAL_LEVEL;
      break;
    case YM2151_REG_KS_AR:
      base = YM2612_REG_RATE_SCALE_AR;
      break;
    case YM2151_REG_AM_D1R:
      base = YM2612_REG_AM_DR;
      break;
    case YM2151_REG_DT2_D2R:
      base = YM2612_REG_SUSTAIN_RATE;
      if ((value >> 6) & 0x03) {
        self->drops.dt2_present = true;
      }
      out_value = value & 0x1F;
      break;
    default:
      base = YM2612_REG_SL_RR;
      break;
  }

  uint8_t offset = (uint8_t)(slot * 4 + channelInPort(target));
  VGMWriter_WriteYM2612(self->writer, (uint8_t)self->map.port[src],
                        (uint8_t)(base + offset), out_value);
}

static void writeKey(FmTranscriber *self, uint8_t value) {
  int src = value & 0x07;
  int target = self->map.target[src];
  if (target < 0) {
    return;
  }
  uint8_t op_mask = (uint8_t)((value & 0x78) << 1);
  uint8_t select = target < YM2612_PORT0_CHANNELS ? (uint8_t)target : (uint8_t)(target + 1);
  VGMWriter_WriteYM2612(self->writer, 0, YM2612_REG_KEY, (uint8_t)(op_mask | select));
}

FmTranscriber *FmTranscriber_Create(const ChannelMap *map, uint32_t clock_2151,
                                    uint32_t clock_2612, VGMWriter *writer,
                                    ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;
  if (!map || !writer) {
    *error = ERR_INTERNAL;
    return NULL;
  }

  FmTranscriber *self = calloc(1, sizeof *self);
  if (!self) {
    *error = ERR_INTERNAL;
    return NULL;
  }
  self->map = *map;
  self->clock_2151 = clock_2151;
  self->clock_2612 = clock_2612;
  self->writer = writer;

  int dropped = 0;
  for (int i = 0; i < 8; i++) {
    self->lr[i] = YM2612_LR_BOTH;
    if (self->map.target[i] < 0) {
      dropped++;
    }
  }
  self->drops.dropped_channels = dropped;
  return self;
}

void FmTranscriber_Destroy(FmTranscriber *self) {
  if (!self) {
    return;
  }
  free(self);
}

void FmTranscriber_Write(FmTranscriber *self, uint8_t reg, uint8_t value) {
  if (!self) {
    return;
  }
  if (reg == YM2151_REG_KEY) {
    writeKey(self, value);
    return;
  }
  if (reg == YM2151_REG_NOISE) {
    if (value & 0x80) {
      self->drops.noise_present = true;
    }
    return;
  }
  if (reg < YM2151_REG_RL_FB_CONN) {
    return;
  }
  if ((reg & 0xE0) == YM2151_REG_RL_FB_CONN) {
    writeChannelGroup(self, reg, value);
    return;
  }
  writeOperatorGroup(self, reg, value);
}

FmDropStats FmTranscriber_Drops(FmTranscriber *self) {
  if (!self) {
    FmDropStats empty = {0, false, false};
    return empty;
  }
  return self->drops;
}
