#pragma once

#include <error_code.h>
#include <stdbool.h>
#include <stdint.h>
#include <vgm_writer.h>

typedef struct PcmStream PcmStream;

PcmStream *PcmStream_Create(uint32_t oki_clock, uint8_t oki_flags, VGMWriter *writer,
                            ErrorCode *error);
void PcmStream_Destroy(PcmStream *self);

void PcmStream_AddDataBlock(PcmStream *self, uint8_t type, const uint8_t *data, uint32_t size);
void PcmStream_DacStream(PcmStream *self, uint8_t command, const uint8_t *operands, uint32_t length);
void PcmStream_OkiWrite(PcmStream *self, uint8_t reg, uint8_t value);
void PcmStream_Seek(PcmStream *self, uint32_t offset);

uint32_t PcmStream_SampleRate(PcmStream *self);
bool PcmStream_HasPcm(PcmStream *self);
