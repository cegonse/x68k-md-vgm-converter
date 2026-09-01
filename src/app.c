#include <app_main.h>
#include <error_code.h>
#include <stdio.h>

int App_Run(int argc, char **argv) {
  const char *program = argc > 0 ? argv[0] : "x68k-md-vgm-conv";

  if (argc < 3) {
    fprintf(stderr,
      "usage: %s <input.vgm> <output.vgm> [--fm-channels a,b,c,...]\n",
      program);
    return ERR_BAD_ARGS;
  }

  fprintf(stderr, "%s: transcriber not yet implemented (stub)\n", program);
  return ERR_UNSUPPORTED;
}
