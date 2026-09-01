#pragma once

#include <error_code.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VGMWriter VGMWriter;

VGMWriter *VGMWriter_Create(uint32_t ym2612_clock, uint32_t rate, ErrorCode *error);
void VGMWriter_Destroy(VGMWriter *self);

void VGMWriter_WriteYM2612(VGMWriter *self, uint8_t port, uint8_t reg, uint8_t value);
void VGMWriter_WaitSamples(VGMWriter *self, uint32_t samples);
void VGMWriter_DataBlock(VGMWriter *self, uint8_t type, const uint8_t *data, uint32_t size);
void VGMWriter_DacStream(VGMWriter *self, uint8_t command, const uint8_t *operands, uint32_t length);
void VGMWriter_MarkLoop(VGMWriter *self);
void VGMWriter_End(VGMWriter *self);

const uint8_t *VGMWriter_Bytes(VGMWriter *self, size_t *size);
bool VGMWriter_Save(VGMWriter *self, const char *path, ErrorCode *error);
