# x68k-md-vgm-conv

A tool to convert X68000 VGM files into Sega Mega Drive VGM files.

## Requirements

- A C99 toolchain (`cc`/`gcc`/`clang`), CMake ≥ 3.22, and `git`.
- `zlib`.
- For the web build: an **Emscripten** toolchain (`emcc`) on `PATH`.
- External tools are fetched by `scripts/init.sh`: **vgmtools**, SGDK's
  **xgmtool** (the downstream XGM converter and end-to-end test gate), and the
  **Cest v5** test runner.

## Usage

```
x68k-md-vgm-conv <input.vgm|.vgz> <output.vgm> [--fm-channels list] [--tempo x]
```

| Argument | Description |
|----------|-------------|
| `<input>` | Source X68000 VGM or gzipped `.vgz`. |
| `<output>` | Destination Mega Drive VGM. |
| `--fm-channels a,b,c,…` | List of YM2151 channel indices (`0`–`7`) to convert. Max 6 (5 when the source has PCM, since YM2612 channel 6 is reserved for DAC). Maps positionally onto YM2612 channels. Omit to keep the first channels by default. |
| `--tempo x` | Playback speed multiplier (`> 0`, default `1.0`). `1.1` plays ~10% faster; pitch is unaffected. |

The `scripts/convert.sh` script can be used to directly generate a XGM file from the source VGM:

```
scripts/convert.sh input.vgm output.xgm [--fm-channels 0,1,2,3,5,7] [--tempo x] [xgmtool flags]
```

## Building

```
make init   # checks dependencies, downloads needed tools, compiles dependencies
make        # build command line tool
make web    # build web version
make test   # run tests
```

The command line tool is generated in `build/`. The web tool is generated in `build/web`.

## How it works

The tool parses the X68000 VGM (transparently decompressing `.vgz` via zlib)
and walks its command stream.

Each YM2151 register write is transcribed to the
equivalent YM2612 write: most fields map register-to-register, but two parts
need care.

Pitch is converted from the YM2151's key-code/key-fraction to the
YM2612's fnum/block using clock-dependent math (the X68000's 4 MHz clock
shifts its scale up ~1.9 semitones versus concert pitch, which is preserved).

Features with no YM2612 equivalent — noise, DT2 detune, timers/CSM/IRQ — are
dropped, and a one-line drop summary is emitted. FM channels are selected by
the keep-list and mapped positionally onto the YM2612's channels.

For PCM samples, the OKIM6258 4-bit ADPCM stream is decoded to linear PCM (using the
XM6 decoder variant, including the per-sample state reset), down-converted to
8-bit unsigned, and emitted as a YM2612 data block driven by DAC-stream
commands on channel 6. 
