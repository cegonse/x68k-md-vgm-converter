#include <vgm_file.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vgm_format.h>
#include <zlib.h>

enum { GZIP_MAGIC_0 = 0x1F, GZIP_MAGIC_1 = 0x8B };

struct VGMFile {
  uint8_t *bytes;
  size_t size;
  uint32_t version;
  uint32_t ym2151_clock;
  uint32_t oki_clock;
  uint8_t oki_flags;
  uint32_t rate;
  uint32_t loop_offset;
  uint32_t loop_samples;
  uint32_t total_samples;
  size_t data_start;
};

static uint32_t readU32(const uint8_t *b, size_t size, size_t off) {
  if (off + 4 > size) {
    return 0;
  }
  return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
         ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

static uint16_t readU16(const uint8_t *b, size_t size, size_t off) {
  if (off + 2 > size) {
    return 0;
  }
  return (uint16_t)(b[off] | (b[off + 1] << 8));
}

static uint8_t readU8(const uint8_t *b, size_t size, size_t off) {
  return off < size ? b[off] : 0;
}

static uint8_t *gunzip(const uint8_t *in, size_t in_size, size_t *out_size,
                       ErrorCode *error) {
  z_stream strm;
  memset(&strm, 0, sizeof strm);
  if (inflateInit2(&strm, 15 + 32) != Z_OK) {
    *error = ERR_INTERNAL;
    return NULL;
  }

  size_t cap = in_size ? in_size * 4 + 1024 : 1024;
  uint8_t *out = malloc(cap);
  if (!out) {
    inflateEnd(&strm);
    *error = ERR_INTERNAL;
    return NULL;
  }

  strm.next_in = (Bytef *)in;
  strm.avail_in = (uInt)in_size;
  strm.next_out = out;
  strm.avail_out = (uInt)cap;

  for (;;) {
    if (strm.avail_out == 0) {
      size_t used = cap;
      uint8_t *grown = realloc(out, cap * 2);
      if (!grown) {
        free(out);
        inflateEnd(&strm);
        *error = ERR_INTERNAL;
        return NULL;
      }
      out = grown;
      cap *= 2;
      strm.next_out = out + used;
      strm.avail_out = (uInt)(cap - used);
    }
    int ret = inflate(&strm, Z_NO_FLUSH);
    if (ret == Z_STREAM_END) {
      break;
    }
    if (ret != Z_OK) {
      fprintf(stderr, "vgm: gzip decompression failed (corrupt or truncated)\n");
      free(out);
      inflateEnd(&strm);
      *error = ERR_PARSE;
      return NULL;
    }
  }

  *out_size = cap - strm.avail_out;
  inflateEnd(&strm);
  return out;
}

static void parseHeader(VGMFile *self) {
  const uint8_t *b = self->bytes;
  size_t n = self->size;
  self->version = readU32(b, n, VGM_HDR_VERSION);
  self->total_samples = readU32(b, n, VGM_HDR_TOTAL_SAMPLES);
  uint32_t loop = readU32(b, n, VGM_HDR_LOOP_OFFSET);
  self->loop_offset = loop ? (uint32_t)(VGM_HDR_LOOP_OFFSET + loop) : 0;
  self->loop_samples = readU32(b, n, VGM_HDR_LOOP_SAMPLES);
  self->rate = readU32(b, n, VGM_HDR_RATE);
  self->ym2151_clock = readU32(b, n, VGM_HDR_YM2151_CLOCK);
  self->oki_clock = readU32(b, n, VGM_HDR_OKIM6258_CLOCK);
  self->oki_flags = readU8(b, n, VGM_HDR_OKIM6258_FLAGS);
  uint32_t data = readU32(b, n, VGM_HDR_DATA_OFFSET);
  self->data_start = data ? (size_t)(VGM_HDR_DATA_OFFSET + data) : VGM_DATA_OFFSET_DEFAULT;
}

VGMFile *VGMFile_ParseBytes(const uint8_t *data, size_t size, ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;

  uint8_t *owned = NULL;
  size_t owned_size = 0;
  if (size >= 2 && data[0] == GZIP_MAGIC_0 && data[1] == GZIP_MAGIC_1) {
    owned = gunzip(data, size, &owned_size, error);
    if (!owned) {
      return NULL;
    }
  } else {
    owned = malloc(size ? size : 1);
    if (!owned) {
      *error = ERR_INTERNAL;
      return NULL;
    }
    memcpy(owned, data, size);
    owned_size = size;
  }

  if (owned_size < 4 || memcmp(owned, "Vgm ", 4) != 0) {
    fprintf(stderr, "vgm: bad magic (not a VGM file)\n");
    free(owned);
    *error = ERR_PARSE;
    return NULL;
  }

  VGMFile *self = calloc(1, sizeof *self);
  if (!self) {
    free(owned);
    *error = ERR_INTERNAL;
    return NULL;
  }
  self->bytes = owned;
  self->size = owned_size;
  parseHeader(self);

  if (self->data_start > self->size) {
    fprintf(stderr, "vgm: data offset 0x%zx beyond end of file\n", self->data_start);
    VGMFile_Destroy(self);
    *error = ERR_PARSE;
    return NULL;
  }
  return self;
}

VGMFile *VGMFile_Parse(const char *path, ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;

  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "vgm: cannot open input '%s'\n", path);
    *error = ERR_OPEN_INPUT;
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    *error = ERR_OPEN_INPUT;
    return NULL;
  }
  long length = ftell(f);
  rewind(f);
  if (length < 0) {
    fclose(f);
    *error = ERR_OPEN_INPUT;
    return NULL;
  }

  uint8_t *buffer = malloc((size_t)length ? (size_t)length : 1);
  if (!buffer) {
    fclose(f);
    *error = ERR_INTERNAL;
    return NULL;
  }
  size_t got = fread(buffer, 1, (size_t)length, f);
  fclose(f);
  if (got != (size_t)length) {
    fprintf(stderr, "vgm: short read on '%s'\n", path);
    free(buffer);
    *error = ERR_OPEN_INPUT;
    return NULL;
  }

  VGMFile *self = VGMFile_ParseBytes(buffer, got, error);
  free(buffer);
  return self;
}

void VGMFile_Destroy(VGMFile *self) {
  if (!self) {
    return;
  }
  free(self->bytes);
  free(self);
}

uint32_t VGMFile_Version(VGMFile *self) { return self->version; }
uint32_t VGMFile_YM2151Clock(VGMFile *self) { return self->ym2151_clock; }
uint32_t VGMFile_OKIClock(VGMFile *self) { return self->oki_clock; }
uint8_t VGMFile_OKIFlags(VGMFile *self) { return self->oki_flags; }
uint32_t VGMFile_Rate(VGMFile *self) { return self->rate; }
uint32_t VGMFile_LoopOffset(VGMFile *self) { return self->loop_offset; }
uint32_t VGMFile_LoopSamples(VGMFile *self) { return self->loop_samples; }
uint32_t VGMFile_TotalSamples(VGMFile *self) { return self->total_samples; }

static int operandLength(uint8_t cmd) {
  if (cmd == 0x61) return 2;
  if (cmd == 0x62 || cmd == 0x63) return 0;
  if (cmd >= 0x70 && cmd <= 0x8F) return 0;
  if (cmd == 0x50) return 1;
  if (cmd >= 0x51 && cmd <= 0x5F) return 2;
  if (cmd >= 0x40 && cmd <= 0x4E) return 2;
  if (cmd == 0x4F) return 1;
  if (cmd >= 0x30 && cmd <= 0x3F) return 1;
  if (cmd >= 0xA0 && cmd <= 0xBF) return 2;
  if (cmd >= 0xC0 && cmd <= 0xDF) return 3;
  if (cmd >= 0xE0) return 4;
  return -1;
}

static int dacStreamOperands(uint8_t cmd) {
  switch (cmd) {
    case VGM_DAC_SETUP: return 4;
    case VGM_DAC_SET_DATA: return 4;
    case VGM_DAC_SET_FREQ: return 5;
    case VGM_DAC_START: return 10;
    case VGM_DAC_STOP: return 1;
    case VGM_DAC_START_FAST: return 4;
    default: return -1;
  }
}

static bool need(VGMFile *self, size_t end, size_t at, ErrorCode *error) {
  if (end <= self->size) {
    return true;
  }
  fprintf(stderr, "vgm: offset 0x%zx: truncated command\n", at);
  *error = ERR_PARSE;
  return false;
}

static bool walkDataBlock(VGMFile *self, const VGMWalker *w, void *ctx,
                          size_t *pos_io, ErrorCode *error) {
  size_t pos = *pos_io;
  const uint8_t *b = self->bytes;
  if (!need(self, pos + 7, pos, error)) {
    return false;
  }
  if (b[pos + 1] != VGM_DATA_BLOCK_COMPAT_BYTE) {
    fprintf(stderr, "vgm: offset 0x%zx: malformed data block header\n", pos);
    *error = ERR_PARSE;
    return false;
  }
  uint8_t type = b[pos + 2];
  uint32_t block_size = readU32(b, self->size, pos + 3);
  if (!need(self, pos + 7 + block_size, pos, error)) {
    return false;
  }
  if (w->data_block) {
    w->data_block(ctx, type, b + pos + 7, block_size);
  }
  *pos_io = pos + 7 + block_size;
  return true;
}

bool VGMFile_Walk(VGMFile *self, const VGMWalker *w, void *ctx, ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;
  if (!self || !w) {
    *error = ERR_INTERNAL;
    return false;
  }

  const uint8_t *b = self->bytes;
  size_t pos = self->data_start;

  while (pos < self->size) {
    uint8_t cmd = b[pos];

    if (cmd == VGM_CMD_END) {
      if (w->end) w->end(ctx);
      return true;
    }
    if (cmd == VGM_CMD_YM2151) {
      if (!need(self, pos + 3, pos, error)) return false;
      if (w->ym2151_write) w->ym2151_write(ctx, b[pos + 1], b[pos + 2]);
      pos += 3;
      continue;
    }
    if (cmd == VGM_CMD_OKIM6258) {
      if (!need(self, pos + 3, pos, error)) return false;
      if (w->oki_write) w->oki_write(ctx, b[pos + 1], b[pos + 2]);
      pos += 3;
      continue;
    }
    if (cmd == VGM_CMD_WAIT) {
      if (!need(self, pos + 3, pos, error)) return false;
      if (w->wait_samples) w->wait_samples(ctx, readU16(b, self->size, pos + 1));
      pos += 3;
      continue;
    }
    if (cmd == VGM_CMD_WAIT_60HZ) {
      if (w->wait_samples) w->wait_samples(ctx, VGM_WAIT_60HZ_SAMPLES);
      pos += 1;
      continue;
    }
    if (cmd == VGM_CMD_WAIT_50HZ) {
      if (w->wait_samples) w->wait_samples(ctx, VGM_WAIT_50HZ_SAMPLES);
      pos += 1;
      continue;
    }
    if (cmd >= VGM_CMD_WAIT_SHORT_MIN && cmd <= VGM_CMD_WAIT_SHORT_MAX) {
      if (w->wait_samples) w->wait_samples(ctx, (uint32_t)(cmd & 0x0F) + 1);
      pos += 1;
      continue;
    }
    if (cmd == VGM_CMD_DATA_BLOCK) {
      if (!walkDataBlock(self, w, ctx, &pos, error)) return false;
      continue;
    }
    if (cmd == VGM_CMD_SEEK_PCM) {
      if (!need(self, pos + 5, pos, error)) return false;
      if (w->pcm_seek) w->pcm_seek(ctx, readU32(b, self->size, pos + 1));
      pos += 5;
      continue;
    }
    if (cmd >= VGM_CMD_DAC_STREAM_MIN && cmd <= VGM_CMD_DAC_STREAM_MAX) {
      int ops = dacStreamOperands(cmd);
      if (!need(self, pos + 1 + (size_t)ops, pos, error)) return false;
      if (w->dac_stream) w->dac_stream(ctx, cmd, b + pos + 1, (uint32_t)ops);
      pos += 1 + (size_t)ops;
      continue;
    }

    int ops = operandLength(cmd);
    if (ops < 0) {
      fprintf(stderr, "vgm: offset 0x%zx: undefined command 0x%02X\n", pos, cmd);
      *error = ERR_PARSE;
      return false;
    }
    if (!need(self, pos + 1 + (size_t)ops, pos, error)) return false;
    pos += 1 + (size_t)ops;
  }

  fprintf(stderr, "vgm: command stream ended without 0x66\n");
  *error = ERR_PARSE;
  return false;
}
