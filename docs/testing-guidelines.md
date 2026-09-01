# Testing Guidelines

How the transcriber is tested. Uses **Cest** v5 (https://cestframework.com/)
- a header-only, Jest-style C++ framework, with its companion **cest-runner**
binary to launch and aggregate the test executables. Tests are C++ (`.cpp`)
even though the tool is C99; the C modules are compiled and linked in, and
Cest drives them. See `cest-reference.md` for the full API surface.

Two layers: **unit tests** per module, and **acceptance tests** that link
the whole app and check real VGM in -> VGM out.

## How Cest is obtained (init.sh, not vendored)

The Cest header and the `cest-runner` binary are downloaded from the v5
GitHub release by `scripts/init.sh` - not committed to the repo:

- Release: https://github.com/cegonse/cest/releases/tag/v5
- The **header** asset is named `cest` (a single header). Fetch it to
  `external/cest/cest` and put that directory on the test include path, so
  tests do `#include <cest>`.
  URL: `https://github.com/cegonse/cest/releases/download/v5/cest`
- The **runner** is a per-platform prebuilt binary. Asset names follow the
  fixed convention `cest-runner-{os}-{arch}` (Windows adds `.exe`):

  | Host OS | Host arch | Asset name | sha256 |
  |---------|-----------|------------|--------|
  | Linux | aarch64/arm64 | `cest-runner-linux-aarch64` | `260ac0ecf5a6223405a71dcb70cf35916c3fb8ffc1f9e8290cc788d254e23755` |
  | Linux | x86_64/amd64 | `cest-runner-linux-x64` | `a227da96cfe59e6a29e8ab390cffdb507abe7ff24c8ace3e7ddd6caac38d956b` |
  | Linux | x86/i686 | `cest-runner-linux-x86` | `6026d144234a756ffdbd37f9000ed937282816a21e8a9fa224e20ef1edd436af` |
  | macOS | arm64 (Apple Silicon) | `cest-runner-macos-aarch64` | `0fca1326fd7382c3186e8719825cf04c7d020e46a57588d8ca80bf8d1cce53c6` |
  | macOS | x86_64 (Intel) | `cest-runner-macos-x64` | `90b2d1304036788ed01780c9022d11006ae861470345efb087990dc250816c18` |
  | Windows | x64 | `cest-runner-windows-x64.exe` | (see release page) |

  Base URL: `https://github.com/cegonse/cest/releases/download/v5/<asset>`

`init.sh` resolves the runner asset deterministically from `uname -s` /
`uname -m` (no API call needed):
- OS: `Linux` → `linux`, `Darwin` → `macos` (Windows/MinGW → `windows`).
- Arch: `x86_64`/`amd64` → `x64`, `aarch64`/`arm64` → `aarch64`,
  `i686`/`i386` → `x86`.
- Download `cest-runner-{os}-{arch}` (+ `.exe` on Windows) to
  `external/cest/cest-runner`, **verify its sha256** against the table,
  mark it `+x`, and smoke-test (`cest-runner --help`).
- Download the `cest` header to `external/cest/cest`.
- Fail clearly if the host maps to no listed asset (unlisted arch) or a
  checksum mismatches, printing expected vs actual hash.

The developer's current machine (macOS) resolves to
`cest-runner-macos-aarch64` or `cest-runner-macos-x64` per its CPU.

(These are prebuilt release assets from github.com; the container's network
allowlist must permit `github.com` / release asset hosts for the download.)

## Cest essentials (see cest-reference.md for the full API)

- **Structure** - one top-level `describe` per file, `it` cases inside,
  nesting allowed:
  ```cpp
  #include <cest>
  #include <oki_adpcm.h>   // C module under test, via <...>

  describe("OkiAdpcm decode", []() {
      it("decodes a known nibble sequence", []() {
          expect(out[0]).toBe(expected0);
      });
  });
  ```
- **Assertions** - `expect(v).toBe(x)`/`toEqual`, `.Not->` to negate,
  `toBeNull()`/`toBeNotNull()`, `toBeInRange(a,b)`, floating-point
  `toBe(x, epsilon)`, and for byte/buffer/register work:
  `toEqualBytes(expected)`, `toEqualMemory(ptr, len)`,
  `toHaveBitsSet(mask)` / `toHaveBitsClear(mask)`.
- **Hooks** - `beforeEach`/`afterEach`, `beforeAll`/`afterAll`, one of each
  per suite, reference-capture `[&]` to reach outer state.
- **Focus/skip** - `fit`/`xit`, `fdescribe`/`xdescribe`, `todo(...)`.
- **Parametrized** - `withParameter<T>().withValue(...).thenDo([](T x){...})`
  for table-driven cases.
- **Failure model** - a failing assertion throws `AssertionError` and stops
  *that* test; other tests continue. Don't use exceptions for normal
  control flow in the tool.
- **Signals/leaks** - the runner reports crashes (SIGSEGV/SIGFPE/...) as
  failures; with ASan on, leaks are reported via `__SANITIZE_ADDRESS__`.

## File naming and layout

- One unit-test file per module, named **`module-name.test.cpp`** (matching
  the module: `oki-adpcm.test.cpp`, `vgm-file.test.cpp`,
  `ym2151-map.test.cpp`, ...).
- Acceptance tests in their own `*.test.cpp` file(s), e.g.
  `acceptance.test.cpp`, using fixture VGMs.
- Suggested tree:
  ```
  tests/
  ├── unit/
  │   ├── vgm-file.test.cpp
  │   ├── oki-adpcm.test.cpp
  │   ├── ym2151-map.test.cpp
  │   ├── pitch.test.cpp
  │   └── md-vgm-writer.test.cpp
  ├── acceptance/
  │   └── acceptance.test.cpp
  └── fixtures/            # sample .vgm inputs + expected outputs
      ├── x68k-fm-only.vgm
      ├── x68k-with-pcm.vgm
      └── expected/…
  external/cest/
  ├── cest                 # header (downloaded)
  └── cest-runner          # runner binary (downloaded)
  ```

## Building the tests (CMake: one binary per test file)

Mirror Cest's own CMake pattern: **glob `*.test.cpp` and make one
executable per file** (`test_<name>`), each linking the C modules under
test and including the downloaded `cest` header. `cest-runner` then
discovers and runs all of them.

```cmake
# --- test targets (mirrors cegonse/cest CMakeLists pattern) ---
enable_testing()

set(CEST_DIR ${CMAKE_SOURCE_DIR}/external/cest)   # populated by init.sh
set(TEST_CFLAGS -g -O0 -Wall -Wunused-value -Werror -std=c++20)

# ASan/LSan (Linux); Darwin uses -fsanitize=address without -static-libasan
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
  list(APPEND TEST_CFLAGS -fsanitize=address)
  set(TEST_LDFLAGS -fsanitize=address -static-libasan)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  list(APPEND TEST_CFLAGS -fsanitize=address)
  set(TEST_LDFLAGS -fsanitize=address)
endif()

# C sources of the tool that tests link against (everything EXCEPT main.c)
file(GLOB TOOL_SOURCES ${CMAKE_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM TOOL_SOURCES ${CMAKE_SOURCE_DIR}/src/main.c)

file(GLOB_RECURSE TEST_FILES ${CMAKE_SOURCE_DIR}/tests/*.test.cpp)

foreach(TEST_FILE ${TEST_FILES})
  get_filename_component(FILE_NAME ${TEST_FILE} NAME_WE)
  set(EXEC_NAME test_${FILE_NAME})
  add_executable(${EXEC_NAME} ${TEST_FILE} ${TOOL_SOURCES})
  target_include_directories(${EXEC_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/inc          # tool headers, included as <name.h>
    ${CEST_DIR})                     # the 'cest' header
  target_compile_options(${EXEC_NAME} PRIVATE ${TEST_CFLAGS})
  target_link_options(${EXEC_NAME} PRIVATE ${TEST_LDFLAGS})
  add_test(NAME ${EXEC_NAME} COMMAND ${EXEC_NAME})
endforeach()
```

Notes:
- `main.c` is **removed** from the sources tests link (see below) - Cest
  provides the test binary's `main()`, so including the tool's `main` would
  cause two `main`s.
- Building test targets in C++20 while the tool is C99 is fine: the C
  modules compile as C and are linked into the C++ test binary. The tool's
  headers stay pure C (no `extern "C"` in them); the test `.cpp` wraps the C
  includes in `extern "C"` at the include site so they link cleanly:
  ```cpp
  #include <cest>
  extern "C" {
  #include <app_main.h>
  #include <oki_adpcm.h>
  }
  ```
- Keep unit vs acceptance separable (e.g. a CMake option or a CTest label)
  so the fast unit set can run without the `external/` tool binaries, while
  acceptance (which shells out to xgmtool) runs when those are built.

## Running the tests (cest-runner)

`cest-runner` launches every test binary in a directory and aggregates
results. After building:

```
external/cest/cest-runner build/            # run all test_* in build/
external/cest/cest-runner build/ --grep pitch
external/cest/cest-runner build/ --watch    # interactive
```

`scripts/` may wrap this (e.g. a `run-tests.sh`) but it's a one-liner over
the build output dir. CTest also works (`ctest` runs each `add_test`), but
the project standard is **cest-runner over the built binaries** for the
richer aggregated/tree output.

## The core entry point (main must be a pass-through)

To let acceptance tests drive the whole app, **`main()` is a thin
pass-through to a callable core function** - no logic in `main`:

```c
// app_main.h
#pragma once
int App_Run(int argc, char **argv);

// main.c   (excluded from the test build)
#include <app_main.h>
int main(int argc, char **argv) { return App_Run(argc, argv); }
```

The acceptance test calls `App_Run(...)` directly with a constructed argv
(input path, `--fm-channels ...`, output path), then inspects the output
file. `main.c` is excluded from the test build at the CMake level (the
`list(REMOVE_ITEM ... main.c)` above), which is the correct fix for the
two-`main` problem. Keep `main.c` a one-liner so nothing of value is lost.

## Unit tests

Test each module's public (opaque-struct) surface in isolation.

- **VGM parser (`vgm-file`)**: header field extraction (YM2151/OKI clocks,
  version, data offset, loop); gzip magic detection; and - most important -
  **command-stream skip/desync resistance**: feed a stream containing every
  command length-class and assert the walker lands exactly on the end
  marker (proves the skip table). `toEqualBytes`/`toEqualMemory` help when
  checking parsed byte ranges.
- **OKIM6258 decoder (`oki-adpcm`)**: a known ADPCM nibble sequence ->
  known PCM output (golden vector), and a **reset-between-samples** test
  (decode two samples; assert the second starts from `signal=-2, step=0`
  and isn't polluted by the first). Consider a parametrized set of
  nibble->delta cases.
- **Pitch (`pitch`)**: KC/KF -> fnum/block against the verified reference
  values in `conversion-mapping.md` (A4 -> block 4 / fnum 1210 at the
  documented clocks; octave doubling keeps fnum and increments block; KF
  fraction moves pitch monotonically). Use floating-point `toBe(x, eps)` or
  assert cents error within ~1 cent. Table-driven via `withParameter`.
- **FM mapping (`ym2151-map`)**: field copies (TL, DT1/MUL, ...); the
  `0xB4` L/R + PMS/AMS repacking (both source fields land in one target
  write, L/R bit-swapped - check with `toHaveBitsSet`); the `0x28` key
  re-encode (never emits channel 3/7); and channel-drop (writes to a
  non-kept channel produce nothing).
- **MD VGM writer (`md-vgm-writer`)**: header bytes (YM2612 clock set, other
  clocks zero - `toEqualBytes` on the header), command framing, data-block
  emission.

**Heap coverage.** Every module that allocates has a test exercising the
**create/destroy pair**, run under ASan so leaks surface. Destructors must
accept `NULL`.

## Acceptance tests (whole-app, always green)

Keep a **working set of acceptance-level tests** at all times: the entire
app is linked and run against example VGM files, verifying input -> output.

- Each takes a fixture `.vgm`, calls `App_Run(...)` with a constructed argv,
  and checks the produced MD VGM:
  - correct header (YM2612 clock, zeroed other chips, loop carried over),
  - expected YM2612 writes for a known-small input (spot-check registers,
    e.g. a known note becomes the expected fnum/block),
  - PCM path: an OKIM6258 fixture yields a type-`0x00` data block +
    DAC-stream commands + DAC enable at the expected native rate,
  - the drop summary reports what was expected to be dropped.
- **xgmtool round-trip gate.** For end-to-end confidence, an acceptance
  test (or a dedicated CI step) feeds the produced MD VGM to `xgmtool`
  (built by `init.sh`) and asserts a valid `.xgm` is produced without
  error. "xgmtool accepts it and it plays" is the ultimate acceptance
  criterion. Keep this separate from the pure-unit run so unit tests don't
  depend on the external toolchain.
- Keep fixtures **small** - a few notes and one short PCM blip - so expected
  outputs are hand-verifiable and tests stay fast. `vgm2txt` (from
  vgmtools) is handy for eyeballing fixtures and produced output when
  writing/maintaining expectations.

## Stub injection for external calls

Make side-effecting calls testable via a **function-pointer seam**:
production injects the real libc-backed implementation, tests inject a stub
they observe. Pattern:

```c
// file.h
#pragma once
#include <stddef.h>
#include <stdint.h>

void File_WriteBytes(const char *path, const uint8_t *data, size_t size);
void File_SetWriteBytesFunction(int (*fn)(const char *, uint8_t *, size_t));
```

- The module holds a static function pointer defaulting to a real
  implementation (`fopen`/`fwrite`/`fclose`); `File_WriteBytes` calls
  through it.
- `File_SetWriteBytesFunction` swaps it. Production never calls the setter
  (the default is real); **tests** install a stub.

```cpp
static int lastSize = 0;
static const char *lastPath = nullptr;
static int stubWrite(const char *path, uint8_t *data, size_t size) {
    lastPath = path; lastSize = (int)size; return 0;   // capture, no disk
}

describe("File_WriteBytes", []() {
    afterEach([]() { File_SetWriteBytesFunction(nullptr); }); // restore default*
    it("passes path and size through", []() {
        File_SetWriteBytesFunction(stubWrite);
        uint8_t buf[4] = {1,2,3,4};
        File_WriteBytes("out.vgm", buf, sizeof(buf));
        expect(lastSize).toBe(4);
    });
});
```
(*the setter should treat `NULL` as "restore the built-in default", or
expose a `File_ResetWriteBytesFunction()` - either way, reset in
`afterEach` so stubs don't leak across tests.)

Seam guidelines:
- Apply to boundaries that are awkward in a test: file reads, file writes,
  and (if the tool ever shells out) process invocation. One setter per
  swappable call; keep the interface tiny.
- The seam is for **boundaries**, not internal logic. Test pure functions
  (pitch, field mapping) directly - no seam.

## Sanitizers

- Test targets build with **ASan/LSan** (and ideally UBSan) as in the CMake
  above; the runner reports leaks via `__SANITIZE_ADDRESS__`. This is what
  makes the create/destroy pair tests meaningful.
- Add a **malformed-input** fixture (truncated header, bad magic, undefined
  command mid-stream) and assert the tool fails cleanly (clear stderr,
  non-zero exit) rather than crashing - run under ASan; the runner will
  also catch any signal as a failure.

## CI order
build tool -> run unit tests (cest-runner over the built unit binaries,
ASan on) -> `init.sh` (build external tools: vgmtools, xgmtool; fetch cest
header + runner) -> run acceptance + xgmtool round-trip.

## What good coverage looks like here
- Every module: constructor/destructor, happy path, one malformed/edge case.
- Parser: full command-skip coverage + desync resistance.
- Decoder: golden PCM vector + reset-between-samples.
- Pitch: reference points within ~1 cent + octave/fraction behavior.
- Mapping: each field class, `0xB4` repack, key re-encode, channel drop.
- Acceptance: FM-only fixture and with-PCM fixture, both round-tripped
  through xgmtool.
