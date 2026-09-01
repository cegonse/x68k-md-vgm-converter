#include <vgm_writer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vgm_format.h>

struct VGMWriter {
  uint32_t ym2612_clock;
  uint32_t rate;
  uint8_t *stream;
  size_t stream_size;
  size_t stream_cap;
  uint32_t total_samples;
  bool has_loop;
  size_t loop_stream_offset;
  uint32_t samples_at_loop;
  uint32_t loop_samples;
  bool ended;
  bool oom;
  uint8_t *built;
};

static bool ensureCap(VGMWriter *self, size_t extra) {
  if (self->stream_size + extra <= self->stream_cap) {
    return true;
  }
  size_t newcap = self->stream_cap ? self->stream_cap : 256;
  while (newcap < self->stream_size + extra) {
    newcap *= 2;
  }
  uint8_t *grown = realloc(self->stream, newcap);
  if (!grown) {
    self->oom = true;
    return false;
  }
  self->stream = grown;
  self->stream_cap = newcap;
  return true;
}

static void appendByte(VGMWriter *self, uint8_t value) {
  if (!ensureCap(self, 1)) {
    return;
  }
  self->stream[self->stream_size++] = value;
}

static void putU32(uint8_t *p, uint32_t value) {
  p[0] = value & 0xFF;
  p[1] = (value >> 8) & 0xFF;
  p[2] = (value >> 16) & 0xFF;
  p[3] = (value >> 24) & 0xFF;
}

VGMWriter *VGMWriter_Create(uint32_t ym2612_clock, uint32_t rate, ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;

  VGMWriter *self = calloc(1, sizeof *self);
  if (!self) {
    *error = ERR_INTERNAL;
    return NULL;
  }
  self->ym2612_clock = ym2612_clock;
  self->rate = rate;
  return self;
}

void VGMWriter_Destroy(VGMWriter *self) {
  if (!self) {
    return;
  }
  free(self->stream);
  free(self->built);
  free(self);
}

void VGMWriter_WriteYM2612(VGMWriter *self, uint8_t port, uint8_t reg, uint8_t value) {
  if (!self) {
    return;
  }
  appendByte(self, port == 0 ? VGM_CMD_YM2612_PORT0 : VGM_CMD_YM2612_PORT1);
  appendByte(self, reg);
  appendByte(self, value);
}

void VGMWriter_WaitSamples(VGMWriter *self, uint32_t samples) {
  if (!self) {
    return;
  }
  self->total_samples += samples;
  while (samples > 0) {
    uint32_t chunk = samples > 0xFFFF ? 0xFFFF : samples;
    appendByte(self, VGM_CMD_WAIT);
    appendByte(self, (uint8_t)(chunk & 0xFF));
    appendByte(self, (uint8_t)(chunk >> 8));
    samples -= chunk;
  }
}

void VGMWriter_DataBlock(VGMWriter *self, uint8_t type, const uint8_t *data, uint32_t size) {
  if (!self) {
    return;
  }
  appendByte(self, VGM_CMD_DATA_BLOCK);
  appendByte(self, VGM_DATA_BLOCK_COMPAT_BYTE);
  appendByte(self, type);
  appendByte(self, (uint8_t)(size & 0xFF));
  appendByte(self, (uint8_t)((size >> 8) & 0xFF));
  appendByte(self, (uint8_t)((size >> 16) & 0xFF));
  appendByte(self, (uint8_t)((size >> 24) & 0xFF));
  if (!ensureCap(self, size)) {
    return;
  }
  memcpy(self->stream + self->stream_size, data, size);
  self->stream_size += size;
}

void VGMWriter_MarkLoop(VGMWriter *self) {
  if (!self) {
    return;
  }
  self->has_loop = true;
  self->loop_stream_offset = self->stream_size;
  self->samples_at_loop = self->total_samples;
}

void VGMWriter_End(VGMWriter *self) {
  if (!self || self->ended) {
    return;
  }
  appendByte(self, VGM_CMD_END);
  self->ended = true;
  if (self->has_loop) {
    self->loop_samples = self->total_samples - self->samples_at_loop;
  }
}

const uint8_t *VGMWriter_Bytes(VGMWriter *self, size_t *size) {
  if (!self || self->oom) {
    if (size) *size = 0;
    return NULL;
  }

  size_t total = VGM_HEADER_SIZE + self->stream_size;
  uint8_t *buffer = calloc(1, total);
  if (!buffer) {
    self->oom = true;
    if (size) *size = 0;
    return NULL;
  }

  memcpy(buffer + VGM_HDR_IDENT, "Vgm ", 4);
  putU32(buffer + VGM_HDR_EOF_OFFSET, (uint32_t)(total - 4));
  putU32(buffer + VGM_HDR_VERSION, VGM_VERSION_1_51);
  putU32(buffer + VGM_HDR_TOTAL_SAMPLES, self->total_samples);
  if (self->has_loop) {
    uint32_t loop_abs = (uint32_t)(VGM_HEADER_SIZE + self->loop_stream_offset);
    putU32(buffer + VGM_HDR_LOOP_OFFSET, loop_abs - VGM_HDR_LOOP_OFFSET);
    putU32(buffer + VGM_HDR_LOOP_SAMPLES, self->loop_samples);
  }
  putU32(buffer + VGM_HDR_RATE, self->rate);
  putU32(buffer + VGM_HDR_YM2612_CLOCK, self->ym2612_clock);
  putU32(buffer + VGM_HDR_DATA_OFFSET, (uint32_t)(VGM_HEADER_SIZE - VGM_HDR_DATA_OFFSET));
  memcpy(buffer + VGM_HEADER_SIZE, self->stream, self->stream_size);

  free(self->built);
  self->built = buffer;
  if (size) *size = total;
  return buffer;
}

bool VGMWriter_Save(VGMWriter *self, const char *path, ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;

  size_t size = 0;
  const uint8_t *bytes = VGMWriter_Bytes(self, &size);
  if (!bytes) {
    *error = ERR_INTERNAL;
    return false;
  }

  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "vgm: cannot open output '%s'\n", path);
    *error = ERR_OPEN_OUTPUT;
    return false;
  }
  size_t written = fwrite(bytes, 1, size, f);
  fclose(f);
  if (written != size) {
    fprintf(stderr, "vgm: short write on '%s'\n", path);
    *error = ERR_OPEN_OUTPUT;
    return false;
  }
  return true;
}
