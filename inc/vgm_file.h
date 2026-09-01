#pragma once

#include <error_code.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VGMFile VGMFile;

typedef struct VGMWalker {
  void (*ym2151_write)(void *context, uint8_t reg, uint8_t value);
  void (*oki_write)(void *context, uint8_t reg, uint8_t value);
  void (*wait_samples)(void *context, uint32_t samples);
  void (*data_block)(void *context, uint8_t type, const uint8_t *data, uint32_t size);
  void (*dac_stream)(void *context, uint8_t command, const uint8_t *operands, uint32_t length);
  void (*pcm_seek)(void *context, uint32_t offset);
  void (*loop_point)(void *context);
  void (*end)(void *context);
} VGMWalker;

VGMFile *VGMFile_Parse(const char *path, ErrorCode *error);
VGMFile *VGMFile_ParseBytes(const uint8_t *data, size_t size, ErrorCode *error);
void VGMFile_Destroy(VGMFile *self);

uint32_t VGMFile_Version(VGMFile *self);
uint32_t VGMFile_YM2151Clock(VGMFile *self);
uint32_t VGMFile_OKIClock(VGMFile *self);
uint8_t VGMFile_OKIFlags(VGMFile *self);
uint32_t VGMFile_Rate(VGMFile *self);
uint32_t VGMFile_LoopOffset(VGMFile *self);
uint32_t VGMFile_LoopSamples(VGMFile *self);
uint32_t VGMFile_TotalSamples(VGMFile *self);

bool VGMFile_Walk(VGMFile *self, const VGMWalker *walker, void *context, ErrorCode *error);
