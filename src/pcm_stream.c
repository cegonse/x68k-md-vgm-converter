#include <pcm_stream.h>

#include <oki_adpcm.h>
#include <stdlib.h>
#include <vgm_format.h>
#include <ym2612.h>

enum {
  OKI_DIVIDER_COUNT = 4,
  PCM_STREAM_CHIP_YM2612 = 0x02,
  YM2612_DAC_ENABLE_BIT = 0x80
};

static const int oki_dividers[OKI_DIVIDER_COUNT] = {1024, 768, 512, 512};

struct PcmStream {
  uint32_t oki_clock;
  uint8_t oki_flags;
  VGMWriter *writer;
  OkiAdpcm *decoder;
  bool dac_enabled;
  bool has_pcm;
};

static uint32_t readLE32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void writeLE32(uint8_t *p, uint32_t value) {
  p[0] = value & 0xFF;
  p[1] = (value >> 8) & 0xFF;
  p[2] = (value >> 16) & 0xFF;
  p[3] = (value >> 24) & 0xFF;
}

static uint8_t to8bit(int16_t sample) {
  int value = (sample >> 6) + 0x80;
  if (value < 0) {
    value = 0;
  } else if (value > 0xFF) {
    value = 0xFF;
  }
  return (uint8_t)value;
}

static void ensureDacEnabled(PcmStream *self) {
  if (self->dac_enabled) {
    return;
  }
  VGMWriter_WriteYM2612(self->writer, 0, YM2612_REG_DAC_ENABLE, YM2612_DAC_ENABLE_BIT);
  self->dac_enabled = true;
}

PcmStream *PcmStream_Create(uint32_t oki_clock, uint8_t oki_flags, VGMWriter *writer,
                            ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;
  if (!writer) {
    *error = ERR_INTERNAL;
    return NULL;
  }

  PcmStream *self = calloc(1, sizeof *self);
  if (!self) {
    *error = ERR_INTERNAL;
    return NULL;
  }
  self->oki_clock = oki_clock;
  self->oki_flags = oki_flags;
  self->writer = writer;
  self->decoder = OkiAdpcm_Create((oki_flags & 0x08) ? 12 : 10, error);
  if (!self->decoder) {
    free(self);
    return NULL;
  }
  return self;
}

void PcmStream_Destroy(PcmStream *self) {
  if (!self) {
    return;
  }
  OkiAdpcm_Destroy(self->decoder);
  free(self);
}

void PcmStream_AddDataBlock(PcmStream *self, uint8_t type, const uint8_t *data, uint32_t size) {
  if (!self || type != VGM_DATA_BLOCK_OKIM6258 || size == 0) {
    return;
  }
  self->has_pcm = true;
  ensureDacEnabled(self);

  size_t sample_count = (size_t)size * 2;
  int16_t *pcm = malloc(sample_count * sizeof(int16_t));
  uint8_t *pcm8 = malloc(sample_count);
  if (!pcm || !pcm8) {
    free(pcm);
    free(pcm8);
    return;
  }

  OkiAdpcm_Reset(self->decoder);
  OkiAdpcm_Decode(self->decoder, data, size, pcm);
  for (size_t i = 0; i < sample_count; i++) {
    pcm8[i] = to8bit(pcm[i]);
  }
  VGMWriter_DataBlock(self->writer, VGM_DATA_BLOCK_YM2612_PCM, pcm8, (uint32_t)sample_count);

  free(pcm);
  free(pcm8);
}

void PcmStream_DacStream(PcmStream *self, uint8_t command, const uint8_t *operands, uint32_t length) {
  if (!self) {
    return;
  }
  switch (command) {
    case VGM_DAC_SETUP: {
      ensureDacEnabled(self);
      uint8_t md[4] = {operands[0], PCM_STREAM_CHIP_YM2612, 0x00, YM2612_REG_DAC_DATA};
      VGMWriter_DacStream(self->writer, VGM_DAC_SETUP, md, 4);
      break;
    }
    case VGM_DAC_SET_DATA: {
      uint8_t md[4] = {operands[0], VGM_DATA_BLOCK_YM2612_PCM, operands[2], operands[3]};
      VGMWriter_DacStream(self->writer, VGM_DAC_SET_DATA, md, 4);
      break;
    }
    case VGM_DAC_SET_FREQ:
      VGMWriter_DacStream(self->writer, VGM_DAC_SET_FREQ, operands, length);
      break;
    case VGM_DAC_START: {
      uint8_t md[10];
      md[0] = operands[0];
      writeLE32(md + 1, readLE32(operands + 1) * 2);
      md[5] = operands[5];
      writeLE32(md + 6, readLE32(operands + 6) * 2);
      VGMWriter_DacStream(self->writer, VGM_DAC_START, md, 10);
      break;
    }
    case VGM_DAC_STOP:
      VGMWriter_DacStream(self->writer, VGM_DAC_STOP, operands, length);
      break;
    case VGM_DAC_START_FAST:
      VGMWriter_DacStream(self->writer, VGM_DAC_START_FAST, operands, length);
      break;
    default:
      break;
  }
}

void PcmStream_OkiWrite(PcmStream *self, uint8_t reg, uint8_t value) {
  (void)self;
  (void)reg;
  (void)value;
}

void PcmStream_Seek(PcmStream *self, uint32_t offset) {
  (void)self;
  (void)offset;
}

uint32_t PcmStream_SampleRate(PcmStream *self) {
  if (!self) {
    return 0;
  }
  int real_div = oki_dividers[self->oki_flags & 0x03] * 2;
  return (self->oki_clock + (uint32_t)(real_div / 2)) / (uint32_t)real_div;
}

bool PcmStream_HasPcm(PcmStream *self) {
  return self ? self->has_pcm : false;
}
