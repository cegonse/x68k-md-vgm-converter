#pragma once

#include <error_code.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct ChannelMap {
  int target[8];
  int port[8];
  bool has_pcm;
} ChannelMap;

bool ChannelMap_Build(const uint8_t *keep, int count, bool source_has_pcm,
                      ChannelMap *out, ErrorCode *error);
