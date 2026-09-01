# CLAUDE.md

Project brief for Claude Code. This file is the map; the detailed specs
live in `docs/`. Read the docs referenced below before implementing the
area they cover — do not work from this summary alone for anything
non-trivial.

## What this project is

A C command-line tool that **transcribes X68000 VGM files into Sega Mega
Drive VGM files**. The X68000 side uses a YM2151 FM chip + OKIM6258 ADPCM;
the Mega Drive side uses a YM2612 FM chip + DAC PCM. The conversion is
intentionally **lossy** (the X68000 has more sound capability).

```
X68000 VGM  ──[this tool]──▶  Mega Drive VGM  ──[xgmtool]──▶  XGM
```

The tool owns only the middle step. Downstream, SGDK's **xgmtool** converts
the MD VGM to XGM and handles PCM resampling to 14 kHz, frame-timing
quantization, and sample dedup. **This tool must not do xgmtool's jobs.**

## Read the docs (map)

| Doc | What it covers | Read before… |
|-----|----------------|--------------|
| `docs/README.md` | Overview, pipeline, decisions | anything |
| `docs/development-guidelines.md` | C99 conventions, style, module pattern, memory rules, layout, the two scripts | writing any code |
| `docs/libraries.md` | External tools + the "don't reimplement this" table | touching tooling/build |
| `docs/formats/vgm.md` | VGM read subset (YM2151 + OKIM6258) | the parser |
| `docs/formats/xgm.md` | XGM format (reference only; xgmtool emits it) | context/validation |
| `docs/chips/ym2151.md` | Source FM register map (MAME-derived) | FM parsing/mapping |
| `docs/chips/ym2612.md` | Target FM register map (BlastEm-derived) | FM emission/mapping |
| `docs/chips/oki-m6258.md` | Source ADPCM: rate math + verified decode (VGMPlay/XM6) | the PCM decoder |
| `docs/chips/sn76489.md` | PSG — unused stub (no X68000 source) | (skip) |
| `docs/conversion-mapping.md` | **The core spec**: field crosswalk, pitch math, channel keep-list, PCM framing, lossy drops | the transcriber |
| `docs/cest-reference.md` | Cest v5 API surface | writing tests |
| `docs/testing-guidelines.md` | Test layout, CMake per-file pattern, cest-runner, stub seams | the test suite |

## Locked decisions (don't relitigate)

- **Decoupled tool + scripts.** The C tool is single-purpose and links
  against none of the external *tools* (vgmtools/xgmtool). Its one linked
  system library is **zlib** (required, for transparent `.vgz`/gzip input);
  `init.sh` verifies zlib up front. `scripts/convert.sh` orchestrates the
  full pipeline to XGM; `scripts/init.sh` sets up external tools so the user
  needs only a C toolchain, CMake, git, and zlib dev files.
- **FM channel selection.** `--fm-channels 0,1,2,3,5,7` — explicit keep-list
  of YM2151 channel indices (0–7), no dupes, max 6 (max **5** when the
  source has PCM). Positional mapping: sorted keep-list → YM2612 channels
  in order.
- **PCM reserves YM2612 channel 6.** When the source has OKIM6258 PCM, ch6
  is auto-reserved for DAC (this is the driver's design), so FM maxes at 5.
- **PCM path = decode in the tool (Option P1).** Decode OKIM6258 4-bit
  ADPCM → linear PCM (16-bit signed), tag it with the native source rate,
  emit as a YM2612 type-`0x00` data block + DAC-stream commands + DAC
  enable. **Do not resample and do not down-convert to 8-bit** — xgmtool
  does both.
- **No resampler dependency.** xgmtool resamples to 14 kHz. Removed from
  scope.
- **SN76489 PSG unused** (no X68000 source equivalent); emit none.
- **Both OKIM6258 delivery paths supported** (direct `0xB7` writes and DAC
  Stream Control `0x90`–`0x95` + type-`0x04` data blocks) for genericity.
- **`main()` is a pass-through** to `App_Run(argc, argv)`; the test build
  excludes `main.c` (Cest provides the test `main`).

## Two hard parts (everything else is mechanical)

The FM field mapping is mostly direct register-to-register copying (see the
table in `conversion-mapping.md`). The two parts that need care:

1. **Pitch conversion** (KC/KF → fnum/block), clock-dependent. Verified
   method in `conversion-mapping.md` §4 (worst-case 0.414 cents). Note the
   X68000's 4 MHz clock shifts its scale up ~1.9 semitones vs concert
   pitch — this is correct and must be preserved.
2. **Lossy drops**: YM2151 noise (no YM2612 noise), DT2 (no YM2612
   detune-2), timers/CSM/IRQ. Documented in `conversion-mapping.md` §7;
   emit a one-line drop summary.

Two known footguns, both in the chip docs: the OKIM6258 per-sample state
reset (`signal=-2, step=0` at each sample boundary — use the XM6 update,
not classic MAME); and the YM2612 `0x28` key-select bank encoding (never
emit channel 3 or 7) plus the frequency latch/commit write order (high
before low).

## Build

CMake, C99, out-of-source. The tool builds independently of the external
tools.

```
cmake -S . -B build
cmake --build build
```

Conventions (full detail in `docs/development-guidelines.md`): C99, libc
first, GNU extensions only if truly required; opaque-struct modules
(`TypeName_Method(self, …)`, `#pragma once`, headers included as
`<name.h>`); prefer static/stack over heap, and every heap allocation gets
a matching destructor and a test; short functions (~15–20 lines) and files
(~150 lines; data tables exempt).

## Set up external tools (once)

`scripts/init.sh` populates git-ignored `external/`:
- clones + builds **vgmtools** (shallow) and **xgmtool** (sparse checkout of
  `Stephane-D/SGDK` `tools/xgmtool/`, builds standalone);
- downloads the **Cest v5** header (`cest`) and the platform-matching
  `cest-runner` binary (sha256-verified) into `external/cest/`.

```
scripts/init.sh
```

## Test

CMake makes one `test_<name>` executable per `*.test.cpp` (mirroring Cest's
own CMakeLists), linking the tool's C sources minus `main.c`. Run them all
with `cest-runner`. Full detail in `docs/testing-guidelines.md`.

```
cmake --build build            # builds the test_* targets too
external/cest/cest-runner build/            # run all tests
external/cest/cest-runner build/ --grep pitch
```

Test build uses AddressSanitizer/LeakSanitizer so the create/destroy tests
catch leaks. Keep the **acceptance** suite green: it links the whole app,
runs it on fixture VGMs via `App_Run(...)`, and (as the end-to-end gate)
feeds the produced MD VGM to `xgmtool` to confirm a valid `.xgm` results.

## Full pipeline (produce an XGM)

```
scripts/convert.sh input.vgm output.xgm [--fm-channels 0,1,2,3,5,7] [xgmtool flags]
```
Stage 1 (this tool) → MD VGM; stage 2 (xgmtool) → XGM. The script stops on
the first failing stage and reports which.

## Workflow

- **Work in small functional batches.** Make a focused, self-contained set
  of changes that can be reviewed on its own, then stop for review before
  continuing. Don't stack unrelated changes into one large drop.
- **Validate after every batch.** After completing a set of changes, run
  `make test` and confirm everything is green before moving on. If tests
  fail, fix them (or report why) before continuing to the next batch.

## Guardrails for Claude Code

- Reuse xgmtool/vgmtools; **don't** reimplement VGM parsing beyond the read
  subset, XGM emission, or PCM resampling (see the table in
  `docs/libraries.md`).
- Don't link the tool against the external tools — they're CLI programs
  invoked by `convert.sh`.
- When a spec detail is needed, open the relevant `docs/` file rather than
  guessing — the chip register layouts and the pitch math are exact and
  source-verified; approximating them silently corrupts output.
- Keep `main.c` a one-line pass-through; put logic in `App_Run`.
