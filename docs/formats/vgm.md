# VGM Format — X68000 Parsing Subset

Reduced from VGM Spec v1.71. This covers **only** what is needed to parse
X68000 VGM files, whose sound hardware is the **YM2151** (FM) and
**OKIM6258** (ADPCM). All other chips, commands, and header fields are
ignored by this converter and are omitted here.

## General rules

- Extension `.vgm`, or `.vgz` if GZip-compressed. Decompress transparently
  (a `.vgm` file may still be gzipped — check the magic bytes `1F 8B`).
- All integers are **unsigned little-endian** ("Intel" byte order).
  `0x12345678` is stored as `78 56 34 12`.
- All header offsets are **relative to the field's own position**.
  e.g. the value at `0x34` gives the VGM data start as `0x34 + value`.
- Time unit is one **sample at 44100 Hz**. "Wait n samples" = n/44100 s.
- Header is 256 bytes (`0x00`–`0xFF`) but fields beyond what a file's
  version defines may be absent. If VGM data starts below `0x100`, treat
  overlapping header bytes as zero.

## Header fields (only the relevant ones)

Offsets not listed below are irrelevant to this converter. Do **not**
assume they are zero when writing — only when reading you may ignore them.

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| `0x00` | 4 | Identifier | Must be `"Vgm "` = `56 67 6D 20`. Reject otherwise. |
| `0x04` | 4 | EOF offset | File length − 4 (relative to `0x04`). |
| `0x08` | 4 | Version | BCD. v1.71 = `0x00000171`. Determines which fields are valid. |
| `0x18` | 4 | Total # samples | Sum of all waits; total playback length. |
| `0x1C` | 4 | Loop offset | Relative to `0x1C`. 0 = no loop. |
| `0x20` | 4 | Loop # samples | Wait-sample count inside the loop. 0 = no loop. |
| `0x24` | 4 | Rate | 50 (PAL) / 60 (NTSC) / 0 (n/a). Informational. |
| `0x30` | 4 | **YM2151 clock** | Hz. Typical `4000000` on X68000 (see note). 0 = no YM2151. Bit 31 set = YM2164 (not expected here). |
| `0x34` | 4 | VGM data offset | Relative to `0x34`. If 0, data starts at `0x40` (pre-1.50). |
| `0x90` | 4 | **OKIM6258 clock** | Hz. Typical `4000000`. 0 = no OKIM6258. |
| `0x94` | 1 | **OKIM6258 flags** | See below. |

### YM2151 clock note
The VGM spec lists `3579545` as "typical", but that is the arcade value.
**X68000 uses a YM2151 clocked at 4 MHz** (`0x003D0900` = 4000000). Read
the actual value from the header; do not hardcode. It matters for
converting YM2151 frequency/KC values to real pitch.

### OKIM6258 flags (`0x94`, 8 bits)
```
bit 0-1  Clock divider select: 0→1024, 1→768, 2→512, 3→512
bit 2    3/4-bit ADPCM select (default 0 = 4-bit; 3-bit unsupported)
bit 3    10/12-bit output    (default 0 = 10-bit)
bit 4-7  reserved (zero)
```
The effective ADPCM sample rate = OKIM6258 clock / divider. Combined with
the per-stream rate this determines resampling on the Mega Drive side.

## Command stream

Starts at `0x34 + value_at_0x34` (or `0x40`). A flat sequence of commands.
Parse every command to advance correctly, but only the ones below carry
data this converter uses — **all others must still be length-skipped**
(see "Skipping unknown commands").

### Commands this converter consumes

| Bytes | Command | Meaning |
|-------|---------|---------|
| `54 aa dd` | **YM2151 write** | Write `dd` to YM2151 register `aa`. |
| `B7 aa dd` | **OKIM6258 write** | Write `dd` to OKIM6258 register `aa`. |
| `67 ...` | **Data block** | PCM/ADPCM payload. See below. |
| `E0 dd dd dd dd` | Seek in PCM data bank | 32-bit LE byte offset into the data bank. |
| `61 nn nn` | Wait n samples | 16-bit LE, 0–65535. |
| `62` | Wait 735 samples | 1/60 s shortcut. |
| `63` | Wait 882 samples | 1/50 s shortcut. |
| `70`–`7F` | Wait n+1 samples | Low nibble n = 0–15, wait n+1. |
| `66` | End of sound data | Stop parsing (respect loop offset for looping). |

### DAC stream control (`0x90`–`0x95`)
X68000 OKIM6258 logs **may** drive PCM playback via DAC Stream Control
rather than (or in addition to) direct `B7` register writes. Support
parsing these to know when/what ADPCM is streamed. Fixed layouts:

| Bytes | Command |
|-------|---------|
| `90 ss tt pp cc` | Setup stream (ss=stream id, tt=chip type, pp/cc=port/reg). |
| `91 ss dd ll bb` | Set stream data (dd=data bank id, ll=step size, bb=step base). |
| `92 ss ff ff ff ff` | Set stream frequency (32-bit LE Hz). |
| `93 ss aa aa aa aa mm ll ll ll ll` | Start stream (aa=start offset, mm=length mode, ll=length). |
| `94 ss` | Stop stream (ss=0xFF stops all). |
| `95 ss bb bb ff` | Start stream fast call (bb=block id, ff=flags). |

For OKIM6258, chip type `tt` in command `0x90` follows the header clock
order. If the log uses these, the ADPCM data lives in a type-`0x04` data
block (below) and is addressed by the stream's start offset.

### Data blocks (`0x67`)
```
67 66 tt ss ss ss ss (data)
```
- `66` = compatibility stop-byte (ignore, it's part of the block header).
- `tt` = data type.
- `ss ss ss ss` = 32-bit LE size of `(data)` in bytes.

Relevant data-block type for this converter:

| `tt` | Contents |
|------|----------|
| `0x04` | **OKIM6258 ADPCM data** for use with associated commands. |

Data blocks may appear anywhere in the stream and must always be parsed
(read `tt` + size, then skip or capture `size` bytes). Multiple type-`0x04`
blocks concatenate into one ADPCM data bank; record each block's start
offset and length within that bank so `E0` seeks and `0x93/0x95` start
offsets resolve correctly. Other data-block types (`0x00` YM2612 PCM,
ROM/RAM dumps `0x80`+, etc.) will not appear in valid X68000 files; if
present, skip them by size.

## Skipping unknown commands

To keep the parser robust, any command not consumed above must be skipped
by its correct operand length. Fixed-length rules:

| Range | Operand bytes to skip |
|-------|-----------------------|
| `61` | 2 |
| `62`, `63`, `66` | 0 |
| `70`–`7F`, `80`–`8F` | 0 (low nibble is the operand) |
| `50` | 1 |
| `51`–`5F` | 2 (register + value) |
| `40`–`4E` | 2 |
| `30`, `31`, `3F`, `4F` | 1 |
| `A0`–`AF` | 2 |
| `B0`–`BF` | 2 |
| `C0`–`C8` | 3 |
| `D0`–`D6` | 3 |
| `E0`, `E1` | 4 |
| `0x32`–`0x3E` | 1 |
| `0xC9`–`0xCF`, `0xD7`–`0xDF` | 3 |
| `0xE2`–`0xFF` | 4 |

If a genuinely undefined command byte is hit (one with no length rule),
stop parsing — the spec says processing must halt immediately. Log the
offset and byte for debugging.

## Parsing checklist

1. Read/verify magic `"Vgm "`; read version.
2. Read YM2151 clock (`0x30`) and OKIM6258 clock + flags (`0x90`/`0x94`).
   A valid X68000 file has YM2151 clock ≠ 0; OKIM6258 clock may be 0 if
   the song has no PCM.
3. Compute data start from `0x34`.
4. Note loop offset (`0x1C`) and loop samples (`0x20`) for XGM loop output.
5. Walk the command stream, capturing:
   - YM2151 register writes (`54`) with their timestamps (accumulated waits),
   - OKIM6258 writes (`B7`) and/or DAC stream control (`90`–`95`),
   - type-`0x04` data blocks into the ADPCM bank,
   - wait commands to build the timeline.
6. Stop at `66` (or loop per the loop offset).
