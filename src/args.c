#include <args.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FM_CHANNELS_MAX = 8, CHANNEL_VALUE_MAX = 255 };

static bool parseTempo(const char *text, double *out, ErrorCode *error) {
  char *end;
  double value = strtod(text, &end);
  if (end == text || *end != '\0' || value <= 0.0) {
    fprintf(stderr, "tempo: invalid multiplier '%s' (must be > 0)\n", text);
    *error = ERR_BAD_ARGS;
    return false;
  }
  *out = value;
  return true;
}

static bool parseChannelList(const char *list, uint8_t *out, int *count, ErrorCode *error) {
  int n = 0;
  const char *p = list;
  while (*p) {
    if (*p < '0' || *p > '9') {
      fprintf(stderr, "channels: invalid channel list '%s'\n", list);
      *error = ERR_BAD_ARGS;
      return false;
    }
    int value = 0;
    while (*p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      if (value > CHANNEL_VALUE_MAX) {
        value = CHANNEL_VALUE_MAX;
      }
      p++;
    }
    if (n >= FM_CHANNELS_MAX) {
      fprintf(stderr, "channels: too many channels (max %d)\n", FM_CHANNELS_MAX);
      *error = ERR_BAD_ARGS;
      return false;
    }
    out[n++] = (uint8_t)value;
    if (*p == ',') {
      p++;
      if (*p == 0) {
        fprintf(stderr, "channels: trailing comma in '%s'\n", list);
        *error = ERR_BAD_ARGS;
        return false;
      }
    } else if (*p != 0) {
      fprintf(stderr, "channels: invalid character in '%s'\n", list);
      *error = ERR_BAD_ARGS;
      return false;
    }
  }
  if (n == 0) {
    fprintf(stderr, "channels: empty channel list\n");
    *error = ERR_BAD_ARGS;
    return false;
  }
  *count = n;
  return true;
}

bool Args_Parse(int argc, char **argv, Args *out, ErrorCode *error) {
  ErrorCode local;
  if (!error) {
    error = &local;
  }
  *error = ERR_NONE;
  if (!out) {
    *error = ERR_INTERNAL;
    return false;
  }

  out->input = NULL;
  out->output = NULL;
  out->fm_channel_count = -1;
  out->tempo = 1.0;

  const char *program = argc > 0 ? argv[0] : "x68k-md-vgm-conv";
  int positional = 0;

  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--fm-channels") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--fm-channels requires a value\n");
        *error = ERR_BAD_ARGS;
        return false;
      }
      if (!parseChannelList(argv[++i], out->fm_channels, &out->fm_channel_count, error)) {
        return false;
      }
    } else if (strncmp(arg, "--fm-channels=", 14) == 0) {
      if (!parseChannelList(arg + 14, out->fm_channels, &out->fm_channel_count, error)) {
        return false;
      }
    } else if (strcmp(arg, "--tempo") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--tempo requires a value\n");
        *error = ERR_BAD_ARGS;
        return false;
      }
      if (!parseTempo(argv[++i], &out->tempo, error)) {
        return false;
      }
    } else if (strncmp(arg, "--tempo=", 8) == 0) {
      if (!parseTempo(arg + 8, &out->tempo, error)) {
        return false;
      }
    } else if (arg[0] == '-' && arg[1] != 0) {
      fprintf(stderr, "unknown option '%s'\n", arg);
      *error = ERR_BAD_ARGS;
      return false;
    } else if (positional == 0) {
      out->input = arg;
      positional++;
    } else if (positional == 1) {
      out->output = arg;
      positional++;
    } else {
      fprintf(stderr, "unexpected argument '%s'\n", arg);
      *error = ERR_BAD_ARGS;
      return false;
    }
  }

  if (!out->input || !out->output) {
    fprintf(stderr,
            "usage: %s <input.vgm> <output.vgm> [--fm-channels a,b,c,...] [--tempo x]\n",
            program);
    *error = ERR_BAD_ARGS;
    return false;
  }
  return true;
}
