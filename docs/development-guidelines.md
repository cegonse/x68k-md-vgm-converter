# Development Guidelines

Conventions for the X68000 → Mega Drive VGM transcriber. This is a single
C command-line tool. Two Bash scripts sit around it: one to set up the
external tools, one to orchestrate the full pipeline to XGM. See
`libraries.md` for what the external tools are and `conversion-mapping.md`
for what the tool actually does.

## Scope reminder

The C tool does **one thing**: read an X68000 VGM (YM2151 + OKIM6258),
transcribe it to a Mega Drive VGM (YM2612 + DAC PCM), and write it out.
It does **not** produce XGM — `xgmtool` does that downstream. It does
**not** resample PCM — `xgmtool` resamples to 14 kHz downstream. Keep the
tool focused; resist pulling downstream concerns into it.

## Language and toolchain

- **C99** (`-std=c99`). This is the target standard; don't rely on C11/C17
  features.
- **GNU extensions**: avoid. Use one only if there's no reasonable C99 way
  to do the job, and note why at the use site. Portability across the
  developer's *IX systems (macOS now, Linux likely) matters more than
  convenience.
- **libc first**: prefer standard library calls for file I/O, string
  handling, and diagnostics (`fopen`/`fread`/`fwrite`, `fprintf(stderr,…)`,
  `<string.h>`, `<stdlib.h>`). Don't reach for platform APIs when libc
  covers it.
- **No non-portable calls** unless wrapped and justified. Nothing that
  ties the build to a single OS.
- **CMake** for the build (`cmake_minimum_required(VERSION 3.22)` to match
  the external tools). Out-of-source builds. The tool builds independently
  of the external tools — it does not link against them.

## Coding style

- **Readability over performance.** No clever tricks, no premature
  optimization. Clear, boring code that's easy to follow wins. This is a
  batch file converter, not a hot loop.
- **No comments in source.** Let descriptive names carry the meaning.
  Do not add comments unless *extremely* necessary — reserved for a genuine
  footgun that names alone cannot convey (a chip quirk, a format edge case).
  Never narrate the code. Default to zero comments.
- **Descriptive names** for variables and functions. `ym2151_channel`, not
  `ch`; `read_data_block`, not `rdb`.
- **Function signature form.** Every *public* function matches
  `return_type Module_Function(type arg_name, type other_arg)` — the
  module/type name in `PascalCase`, an underscore, then the function in
  `PascalCase`. **Declarations name their parameters**, not just their
  types. This applies to the app entry point too:
  `int App_Run(int argc, char **argv)` (the one function `main()` calls).
- **Static (file-local) functions use `lowerCamelCase`:** `readHeader()`,
  `appendByte()`, `operandLength()`. The `Module_` prefix is reserved for
  the public surface, so the casing tells you at a glance whether a function
  is part of a module's API or a private helper.
- **No domain magic numbers — back them with a named enum.** Command bytes,
  register numbers, header field offsets, format versions and the like get
  a named `enum` value (e.g. `VgmCommand`, `VgmHeaderField`,
  `Ym2612Register`) rather than an inline hex literal. Introduce the enum
  value when the code first needs it; don't pre-populate unused constants.
  Genuinely structural tables (e.g. an opcode length-classification lookup)
  may stay numeric where naming each boundary would only obscure them.
- **Short functions.** Aim for 15–20 lines. Not a hard rule, but if a
  function grows past that, look for a block to extract.
- **Indentation: 2 spaces, no tabs.** One additional 2-space level per
  nesting depth.
- **Indent continuations by a block, don't align to parentheses.** When a
  call or expression wraps, indent the continuation lines by one extra
  2-space level rather than lining them up under the opening `(`. Keeps
  diffs small and lines short.
- **Opening brace on the same line (K&R).** For function definitions and
  control blocks alike, the `{` sits next to the `)` / keyword, not on its
  own line. Example:
  ```c
  int App_Run(int argc, char **argv) {
    if (argc < 3) {
      fprintf(stderr,
        "usage: %s <input.vgm> <output.vgm>\n",
        program);
      return ERR_BAD_ARGS;
    }
    return ERR_NONE;
  }
  ```
- **`static inline` freely.** Extract logical blocks into `static inline`
  helpers in the `.c` file to keep the main function readable. This is
  encouraged, not exceptional.
- **Short files.** Aim for ~150 lines per file. Not strict. **Exception:**
  data-only files (large constant tables, e.g. ADPCM step tables) are
  exempt — they can be as long as the data requires.
- **Includes use global scope.** Favor `#include <foo.h>` over
  `#include "foo.h"`. Configure the build with `-I` include paths
  (`target_include_directories`) so headers resolve globally. This applies
  to the project's own headers too.

## Module pattern — opaque structs

Encapsulate behavior behind opaque struct pointers. The header exposes the
type name and functions; the definition lives in the `.c` file. This keeps
internals private and gives every module a clear surface.

```c
// pcm_file.h
#pragma once
#include <error_code.h>
#include <stdint.h>

typedef struct PCMFile PCMFile;

PCMFile *PCMFile_Parse(const char *path, ErrorCode *error);
void PCMFile_Destroy(PCMFile *self);
uint16_t PCMFile_SampleRate(PCMFile *self);
uint8_t *PCMFile_SampleData(PCMFile *self);
```

Conventions this illustrates:
- `#pragma once` in every header.
- **No `extern "C"` in the tool's own headers or `.c` sources** — the tool
  is pure C. The only C++ in the project is the test files; when a test
  `.cpp` includes a C header, it wraps the include in `extern "C"` at the
  include site (see `testing-guidelines.md`). Keep the linkage concern on
  the C++ side, out of the C sources.
- One opaque type per module, named in `PascalCase`.
- Functions namespaced `TypeName_Method`, taking `self` as the first
  parameter (the "class instance" idiom).
- Constructors return a heap pointer (or `NULL` on failure) and take an
  `ErrorCode *` out-parameter for failure detail.
- Every constructor has a matching destructor. See memory rules below.
- Accessors are named for what they return (`SampleRate`, `SampleData`).

## Memory management

- **Prefer static/stack over heap.** Where a module can use fixed internal
  storage or stack buffers instead of allocating, do so. Less to leak, less
  to test, simpler lifetimes. (VGM data can be large, so streaming buffers
  and the loaded file itself will be heap — that's fine; it's the many
  small incidental allocations to avoid.)
- **When you do use the heap:**
  - Always pair creation with destruction: a `_Create`/`_Parse` has a
    matching `_Destroy`. No orphan allocators.
  - Every heap-using module must be **covered by tests** that exercise the
    create/destroy pair (so leaks show up under a leak checker / ASan).
  - Destructors accept `NULL` harmlessly (so cleanup paths are simple).
  - Set freed pointers' owners to a clear state; don't leave dangling
    references in structs.
- Free in reverse order of acquisition; on partial-construction failure,
  unwind cleanly (free what was allocated so far, return `NULL`).

## Error handling

- Use a shared `ErrorCode` type (out-parameter) for recoverable failures
  in constructors/parsers, as shown above. Define the codes centrally
  (`error_code.h`).
- Report user-facing errors to `stderr` with `fprintf`; keep `stdout` for
  actual program output where relevant.
- Fail loudly and early on malformed input — a bad VGM should produce a
  clear diagnostic (offset + what was expected), not a crash or silent
  wrong output. The VGM parser should stop on undefined commands per the
  format spec.
- Exit codes: `0` success, non-zero on failure (distinct codes for
  can't-open-input, can't-open-output, parse-error, etc., mirroring
  xgmtool's convention so scripts can branch on them).

## Project layout

```
.
├── CMakeLists.txt
├── src/                  # tool source (.c)
├── inc/                  # tool headers (.h), included via <...>
├── tests/                # Cest tests (see testing-guidelines.md)
├── scripts/
│   ├── init.sh           # clone + build external tools into external/
│   └── convert.sh        # full X68K VGM → XGM orchestration
├── external/             # created by init.sh; git-ignored
│   ├── vgmtools/
│   ├── xgmtool/          # sparse checkout of SGDK tools/xgmtool
│   └── cest/             # Cest header + cest-runner (from v5 release)
└── docs/                 # this documentation set
```

- `external/` is **git-ignored** and populated by `init.sh`. Never vendor
  the external tool sources into the repo.
- The tool's own headers go in `inc/` and are included as `<name.h>` with
  `inc/` on the `-I` path.

## The two scripts

### `scripts/init.sh` — environment setup
Gets external tools ready so the user needs nothing pre-installed beyond a
C toolchain, CMake, and git.

Responsibilities:
1. Create `external/` if absent.
2. Clone/update **vgmtools** (`vgmrips/vgmtools`) into `external/vgmtools`.
   Shallow clone (`--depth 1`) is fine.
3. Clone **xgmtool** from SGDK (`Stephane-D/SGDK`) into `external/xgmtool`.
   Prefer a **sparse checkout** of just `tools/xgmtool/` to avoid pulling
   all of SGDK; fall back to a full `--depth 1` clone if sparse checkout
   isn't available. (xgmtool's `CMakeLists.txt` builds standalone.)
4. Build each tool with the installed toolchain via its own CMake
   (out-of-source build dirs under `external/*/build`).
5. Fetch **Cest v5** test dependencies into `external/cest/` from the
   release https://github.com/cegonse/cest/releases/tag/v5 : the header
   asset (named `cest`) and the platform-matching `cest-runner` binary
   (select from the release's asset list by host OS/arch — don't hardcode
   the suffix; mark the runner executable). See `testing-guidelines.md`.
6. Verify the expected artifacts exist afterward — the built tool binaries
   **and** `external/cest/cest` + a runnable `cest-runner` — and report
   their paths; exit non-zero with a clear message if anything is missing.

Guidelines:
- Idempotent: re-running updates rather than breaks. Don't re-clone if the
  directory already exists — fetch/checkout instead.
- POSIX `sh`-compatible where practical; if bash features are used, set
  `#!/usr/bin/env bash` and `set -euo pipefail`.
- Detect missing prerequisites (git, cmake, C compiler) up front and give
  an actionable error.
- Pin nothing by default (track upstream master), but keep the clone step
  in one place so a future commit-pin is a one-line change.

### `scripts/convert.sh` — full pipeline orchestration
Produces a final `.xgm` from an X68000 `.vgm`, chaining the tools so the
user runs one command.

Pipeline:
1. **This tool**: X68000 `.vgm` → Mega Drive `.vgm` (intermediate).
2. Optionally **vgmtools** for any pre/post-conditioning if needed (e.g.
   `opt_oki` on the source, or a cleanup pass — only if it proves useful;
   start without it).
3. **xgmtool**: Mega Drive `.vgm` → `.xgm` (`xgmtool in.vgm out.xgm`).
   xgmtool handles PCM resampling to 14 kHz, frame quantization, and
   sample dedup — the tool does not.

Guidelines:
- `set -euo pipefail`; stop on the first failing stage and report which
  stage failed (branch on the tools' exit codes).
- Pass through the tool's own flags (notably the FM channel keep-list) to
  stage 1; expose xgmtool's NTSC/PAL and sample flags (`-n`/`-p`/`-di`/
  `-dr`) where the user may need them.
- Use a temp file / temp dir for the intermediate MD VGM; clean up on exit
  (trap), but support a `--keep-intermediate` flag for debugging.
- Locate tool binaries under `external/*/build` (from `init.sh`); error
  clearly if they're missing and point the user at `init.sh`.

## What not to do

- Don't reimplement VGM parsing beyond the X68000 read subset the tool
  needs, and don't reimplement XGM emission or PCM resampling — those are
  xgmtool's job (see `libraries.md`).
- Don't link the tool against vgmtools/xgmtool — they're separate CLI
  programs invoked by `convert.sh`, not libraries.
- Don't optimize the transcriber for speed at the cost of clarity.
- Don't add heap allocation without a destructor and a test.
