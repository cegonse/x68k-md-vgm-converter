# Libraries and External Tools

What the project depends on, what each dependency is for, and — critically
— what each one does so the C tool doesn't reimplement it. All external
tools are cloned and built by `scripts/init.sh` into `external/`; none is
linked into the C tool. They are invoked as CLI programs by
`scripts/convert.sh`.

## Division of labor (read this first)

The C tool is deliberately small because the external tools already do
most of the surrounding work. Before writing any conversion/resampling/
format code, check whether one of these already covers it — the rule is
**reuse, don't reimplement**.

| Concern | Who does it |
|---------|-------------|
| Parse X68000 VGM (YM2151 + OKIM6258) | **The C tool** (read subset) |
| YM2151 → YM2612 register transcription | **The C tool** (core job) |
| OKIM6258 ADPCM → raw PCM decode | **The C tool** |
| Emit Mega Drive VGM (YM2612 + DAC PCM) | **The C tool** |
| VGM → XGM conversion | **xgmtool** |
| PCM resample to 14 kHz + 256-align | **xgmtool** (`resample(...,14000,256,...)`) |
| Frame-timing quantization | **xgmtool** (`VGM_convertWaits`) |
| Sample dedup / auto-ignore | **xgmtool** (`sampleIgnore`, default on) |
| XGM → binary (.xgc/.bin) compile | **xgmtool** (only if you want the compiled form) |
| OKIM6258 stream optimization (optional) | **vgmtools** `opt_oki` |
| VGM → text (debug / fixtures) | **vgmtools** `vgm2txt` |

## xgmtool

- **Source**: `Stephane-D/SGDK`, path `tools/xgmtool/`. Current version
  1.76 (2024). Author: Stéphane Dallongeville.
- **Build**: self-contained `CMakeLists.txt` inside `tools/xgmtool/`
  (`cmake_minimum_required(VERSION 3.22)`, globs `src/*.c`, links `libm`).
  Builds standalone — does **not** need the rest of SGDK. On 64-bit hosts
  the 32-bit (`-m32`) branch is skipped, so no cross-compile setup needed.
- **Clone strategy**: SGDK is large; sparse-checkout just `tools/xgmtool/`
  (fall back to `--depth 1` full clone). See `init.sh` guidance in the
  development guidelines.
- **What it does** (from `xgmtool.c`): the action is chosen by input/output
  file extensions.
  - `xgmtool in.vgm out.vgm` — optimize a Mega Drive VGM.
  - `xgmtool in.vgm out.xgm` — **convert MD VGM → XGM** (our main use).
    Internally: `VGM_create` → `VGM_convertWaits` → `VGM_cleanCommands`
    → `VGM_cleanSamples` → `VGM_fixKeyCommands` → `XGM_createFromVGM`
    → `XGM_asByteArray`.
  - `xgmtool in.vgm out.xgc` (or `.bin`) — convert **and compile** to the
    Z80-driver binary.
  - `xgmtool in.xgm out.vgm` — reverse (XGM → VGM), and other experimental
    XGC paths.
- **Relevant flags**:
  - `-n` / `-p` — force NTSC / PAL timing (meaningful for VGM→XGM).
  - `-di` — disable PCM sample auto-ignore (use when PCM aren't extracted
    properly).
  - `-dr` — disable PCM sample-rate auto-fix (use when PCM aren't extracted
    properly).
  - `-dd` — disable delayed KEY OFF when KEY ON/OFF happen in one frame
    (can fix instrument sound).
  - `-s` / `-v` — silent / verbose.
- **Key consequence for the C tool**: because xgmtool resamples to 14 kHz
  and fixes/ignores samples itself, the C tool must **not** resample and
  must **not** try to pre-quantize timing. Hand xgmtool a clean MD VGM with
  PCM at the source (OKIM6258-native) rate, correctly tagged, and let it
  finish. If xgmtool mishandles the PCM, the fix is usually a flag
  (`-di`/`-dr`), not tool code.

## vgmtools

- **Source**: `vgmrips/vgmtools`. GPL-2.0. Author: Valley Bell.
- **Build**: top-level `CMakeLists.txt`; bundles its own `zlib`. Produces a
  set of **standalone CLI executables** — it is **not** a library. There is
  no `libvgm` to link. (Shared headers `vgm_lib.h`, `VGMFile.h`, `common.h`
  exist but each tool has its own `main`.) Shallow clone is fine.
- **Tools we may use**:
  - **`opt_oki` — OKIM6258 optimizer.** Scans streamed OKIM6258 data from
    X68000-game VGM logs and turns it into "play sample" commands for the
    VGM DAC-Stream system. This is directly in our problem domain and may
    be useful as an optional pre-conditioning step on the source VGM.
    **Caveat**: it needs the source VGM to log X68000 DMA commands, which
    only XM6 VGM-mod (2021+) and MAME VGM-mod 0.236+ produce. Don't assume
    all X68000 VGMs qualify. Start the pipeline **without** it; add it only
    if it demonstrably helps a given source.
  - **`vgm2txt` — VGM text writer.** Dumps a VGM to human-readable text,
    with **YM2151 note-name support**. Excellent for building/inspecting
    test fixtures and debugging both the input parse and the MD VGM output.
    Not part of the production pipeline; a development/testing aid.
- **Other tools present** (not currently used, listed so they're not
  reinvented): `vgm_cmp` (compressor), `vgm_facc` (frame-accurate rounding
  — has strong "don't use unless you know exactly why" warnings),
  `vgmlpfnd` (loop finder), `vgm_trim` (trimmer), `vgm_ptch` (header/chip
  patcher), plus many format-specific converters/optimizers.

## Testing — Cest (v5)

- **Framework**: Cest v5 (https://cestframework.com/), MIT-licensed. A
  header-only, Jest-style C++ framework used for the C tool's unit and
  acceptance tests, plus its companion **`cest-runner`** binary that
  launches and aggregates the compiled test executables.
- **Obtained by `init.sh`, not vendored.** Both come from the v5 GitHub
  release (https://github.com/cegonse/cest/releases/tag/v5):
  - the **header** asset (named `cest`) → `external/cest/cest`, put on the
    test include path so tests `#include <cest>`;
  - the platform-matching **`cest-runner`** binary → `external/cest/`,
    selected from the release asset list by host OS/arch (don't hardcode
    the suffix), made executable, and smoke-tested (`--help`).
- **Build/run model**: CMake makes one `test_<name>` executable per
  `*.test.cpp` (mirroring Cest's own CMakeLists), and `cest-runner` runs
  them all. The API surface is captured in `cest-reference.md`; wiring,
  the per-file CMake pattern, the download steps, and test conventions are
  in `testing-guidelines.md`.

## PCM sample-rate conversion — not needed

Originally a resampler was on the dependency list. It has been **removed
from scope**: xgmtool resamples PCM to 14 kHz internally
(`resample(bank->data, …, sample->rate, 14000, 256, …)` in `xgmsmp.c`).
The C tool only needs to **decode OKIM6258 ADPCM to raw PCM** and tag the
data block with the correct source sample rate; xgmtool does the rest. Do
not add a resampling library.

## Prerequisites the user must have

`init.sh` builds everything else, but the user's system must provide:
- a C toolchain (C99-capable; clang on macOS, gcc/clang on Linux),
- **CMake** ≥ 3.22,
- **git** (with sparse-checkout support preferred for the xgmtool clone),
- a POSIX shell / bash for the scripts.

No pre-installed VGM/XGM tooling is required — that's what `init.sh` is
for.
