# X68000 → Mega Drive VGM Transcriber — Documentation

Documentation set for building a C command-line tool that transcribes
X68000 VGM files (YM2151 FM + OKIM6258 ADPCM) into Sega Mega Drive VGM
files (YM2612 FM + DAC PCM), for downstream conversion to XGM via SGDK's
`xgmtool`.

## Pipeline

    X68000 VGM  ──[this tool]──▶  Mega Drive VGM  ──[xgmtool]──▶  XGM

This tool owns only the middle step: chip transcription. `xgmtool` handles
VGM→XGM, PCM resampling to 14 kHz, frame-timing quantization, and sample
dedup. See `libraries.md` for the full division of labor.

## Documents

Start with `../CLAUDE.md` (the top-level brief and map), then:

- `development-guidelines.md` — C99 conventions, coding style, module
  patterns, project layout, the two Bash scripts (`init.sh`, `convert.sh`).
- `libraries.md` — external tools (vgmtools, xgmtool, Cest) and the
  "don't reimplement this" table.
- `formats/vgm.md` — VGM format, reduced to the X68000 read subset
  (YM2151 + OKIM6258) the tool parses.
- `formats/xgm.md` — XGM format, reference/appendix (xgmtool emits it;
  useful for validation and understanding the downstream target).
- `chips/ym2151.md` — YM2151 register map, the source FM chip (MAME-derived).
- `chips/ym2612.md` — YM2612 register map, the target FM chip (BlastEm-derived).
- `chips/oki-m6258.md` — OKIM6258 ADPCM: rate math + verified decode
  (VGMPlay / XM6 variant), the source PCM.
- `chips/sn76489.md` — SN76489 PSG stub (unused; no X68000 source).
- `conversion-mapping.md` — **the core spec**: YM2151→YM2612 register
  crosswalk, channel keep-list semantics, verified pitch math, and
  OKIM6258→DAC PCM representation.
- `cest-reference.md` — Cest v5 API surface (condensed in-repo copy).
- `testing-guidelines.md` — test layout, the CMake per-file pattern,
  cest-runner + header download, stub-injection seams, the xgmtool gate.

## Key decisions already made

- Lossy conversion (X68000 has more capability than Mega Drive).
- User picks which YM2151 channels to keep via `--fm-channels 0,1,2,3,5,7`
  (indices 0–7, no dupes, max 6 — or max 5 when PCM is present).
- When source has PCM, YM2612 channel 6 is auto-reserved for DAC.
- Positional channel mapping (sorted keep-list → YM2612 channels in order).
- Fixed PCM target rate is 14 kHz — but enforced by xgmtool, not this tool.
  This tool only decodes OKIM6258 ADPCM to linear PCM and tags the native
  rate (PCM path = decode in the tool, "Option P1").
- Both OKIM6258 delivery paths (direct register writes and DAC Stream
  Control) are supported for genericity.
- SN76489 PSG unused.
- Tool is fully decoupled; `convert.sh` orchestrates, `init.sh` sets up
  external tools (sparse checkout of xgmtool from SGDK; sha256-verified
  Cest v5 header + runner).
