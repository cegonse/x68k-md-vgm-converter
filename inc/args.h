#pragma once

#include <error_code.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Args {
  const char *input;
  const char *output;
  uint8_t fm_channels[8];
  int fm_channel_count;  /* -1 when --fm-channels was not given */
  double tempo;          /* playback speed multiplier; 1.0 = faithful */
} Args;

bool Args_Parse(int argc, char **argv, Args *out, ErrorCode *error);
