#include <cest>

extern "C" {
#include <app_main.h>
#include <error_code.h>
}

describe("App_Run (stub)", []() {
  it("rejects missing arguments with ERR_BAD_ARGS", []() {
    char program[] = "x68k-md-vgm-conv";
    char *argv[] = { program };
    expect(App_Run(1, argv)).toBe((int)ERR_BAD_ARGS);
  });
});
