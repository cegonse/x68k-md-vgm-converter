#pragma once

#include <channel_map.h>
#include <error_code.h>
#include <stdbool.h>
#include <stdint.h>
#include <vgm_writer.h>

typedef struct FmDropStats {
  int dropped_channels;
  bool noise_present;
  bool dt2_present;
} FmDropStats;

typedef struct FmTranscriber FmTranscriber;

FmTranscriber *FmTranscriber_Create(const ChannelMap *map, uint32_t clock_2151,
                                    uint32_t clock_2612, VGMWriter *writer,
                                    ErrorCode *error);
void FmTranscriber_Destroy(FmTranscriber *self);
void FmTranscriber_Write(FmTranscriber *self, uint8_t reg, uint8_t value);
FmDropStats FmTranscriber_Drops(FmTranscriber *self);
