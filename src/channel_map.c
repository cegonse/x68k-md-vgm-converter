#include <channel_map.h>

#include <stdio.h>

enum {
  YM2151_CHANNELS = 8,
  YM2612_FM_CHANNELS = 6,
  YM2612_PORT0_CHANNELS = 3,
  CHANNEL_MAP_MAX_FM = 6,
  CHANNEL_MAP_MAX_FM_WITH_PCM = 5
};

static void sortAscending(uint8_t *values, int count) {
  for (int i = 1; i < count; i++) {
    uint8_t value = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > value) {
      values[j + 1] = values[j];
      j--;
    }
    values[j + 1] = value;
  }
}

bool ChannelMap_Build(const uint8_t *keep, int count, bool source_has_pcm,
                      ChannelMap *out, ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;
  if (!out) {
    *error = ERR_INTERNAL;
    return false;
  }

  for (int i = 0; i < YM2151_CHANNELS; i++) {
    out->target[i] = -1;
    out->port[i] = -1;
  }
  out->has_pcm = source_has_pcm;

  int max = source_has_pcm ? CHANNEL_MAP_MAX_FM_WITH_PCM : CHANNEL_MAP_MAX_FM;
  if (count < 0 || count > max) {
    fprintf(stderr, "channels: keep-list has %d channels (max %d%s)\n", count, max,
            source_has_pcm ? "; PCM reserves YM2612 ch6" : "");
    *error = ERR_BAD_ARGS;
    return false;
  }
  if (count > 0 && !keep) {
    *error = ERR_INTERNAL;
    return false;
  }

  bool seen[YM2151_CHANNELS] = {false};
  uint8_t sorted[CHANNEL_MAP_MAX_FM];
  for (int i = 0; i < count; i++) {
    uint8_t channel = keep[i];
    if (channel >= YM2151_CHANNELS) {
      fprintf(stderr, "channels: index %u out of range 0-7\n", channel);
      *error = ERR_BAD_ARGS;
      return false;
    }
    if (seen[channel]) {
      fprintf(stderr, "channels: duplicate index %u\n", channel);
      *error = ERR_BAD_ARGS;
      return false;
    }
    seen[channel] = true;
    sorted[i] = channel;
  }

  sortAscending(sorted, count);

  for (int i = 0; i < count; i++) {
    int source = sorted[i];
    out->target[source] = i;
    out->port[source] = i < YM2612_PORT0_CHANNELS ? 0 : 1;
  }
  return true;
}
