# XGM Format — Output Target (v1.01)

XGM = "eXtended Genesis Music", the music driver by Stéphane Dallongeville
for the Sega Mega Drive / Genesis, part of **SGDK**. This converter emits
the **source XGM file format** (`.xgm`), which SGDK's `xgmtool` compiles
into the binary `.bin`/`.xgc` the Z80 driver actually plays. We generate
the `.xgm`; we do **not** generate the compiled binary.

Unlike the VGM doc (which is a read-only subset), this is the format we
**write**, so it's documented in full with the hard constraints flagged.

## Driver capabilities (the target we're mapping onto)

- Runs entirely on the Z80; leaves the 68000 free.
- Supports **FM** (YM2612) and **PSG** (SN76489).
- Up to **4 PCM channels**, **8-bit signed, played at 14 kHz**, via
  software mixing into the YM2612 DAC — this **replaces the 6th FM
  channel**. So the maximum simultaneous layout is **5 FM + 4 PCM + 4 PSG
  = 13 channels**.
- PCM samples may exceed 32 KB. **Hard constraint: every sample's address
  and size must be aligned to 256 bytes.**
- (SFX playback with 16 priority levels exists but is a runtime/68000
  feature; the converter doesn't need to emit SFX.)

### Consequences for this converter
- The "reserve YM2612 ch6 for DAC when PCM present" rule in the conversion
  spec is exactly the driver's "PCM replaces 6th FM channel" behavior.
- OKIM6258 ADPCM must end up as **8-bit signed PCM resampled to 14 kHz**.
  This fixes the target sample rate for the ADPCM pipeline.
- Each converted sample must be **padded to a 256-byte boundary**, and its
  offset within the sample bloc must also be 256-aligned.

## File layout

Multi-byte values are **little-endian**. `.xgm` may be gzip-compressed as
`.xgz` (we emit uncompressed `.xgm` by default).

| Address | Size | Field |
|---------|------|-------|
| `$0000` | 4 | Ident, must be `"XGM "`. |
| `$0004` | 252 | **Sample id table** — 63 entries × 4 bytes. |
| `$0100` | 2 | **SLEN** = sample data bloc size / 256. |
| `$0102` | 1 | Version = `0x01`. |
| `$0103` | 1 | Flags (see below). |
| `$0104` | SLEN×256 | **Sample data bloc** (8-bit signed PCM). |
| `$0104 + SLEN` | 4 | **MLEN** = music data bloc size (in bytes). |
| `$0108 + SLEN` | MLEN | **Music data bloc** (XGM commands). |
| `$0108 + SLEN + MLEN` | var | GD3 tags (optional, if flag bit 1 set). |

Note the unit mismatch: **SLEN is in 256-byte units**, but in the address
column it multiplies out to bytes; **MLEN is a raw byte count**.

### Sample id table (`$0004`, 252 bytes)
63 possible samples, 4 bytes each:
```
entry+$0 (2 bytes): sample address / 256   (relative to start of sample bloc, $0104)
entry+$2 (2 bytes): sample size / 256
```
Low 8 bits are dropped because address and size are always 256-aligned.
An **empty entry** has address = `$FFFF`, size = `$0001`.

Sample IDs are **1-based** in play commands (id 0 is reserved to mean
"stop channel"), but the table is indexed by `(id − 1)`:
```
sample address = (table[(id-1)*4 + 0] << 8) | (table[(id-1)*4 + 1] << 16)
sample size    = (table[(id-1)*4 + 2] << 8) | (table[(id-1)*4 + 3] << 16)
```

### Flags byte (`$0103`)
```
bit 0  NTSC/PAL: 0 = NTSC (frame wait = 1/60 s), 1 = PAL (1/50 s)
bit 1  GD3 tags present: 0 = no, 1 = yes (located after music data)
bit 2  Multi-track file: 0 = no, 1 = yes
bit 3-7 reserved (0)
```
The converter sets NTSC/PAL from the source (VGM `Rate` field / user
flag), normally emits a single track (bit 2 = 0), and GD3 is optional.

### Sample data bloc (`$0104`)
All sample data concatenated, **8-bit signed**, each sample padded so both
its start offset and length are multiples of 256. If SLEN = 0 the bloc is
empty (song has no PCM) and the field is skipped.

## XGM music commands

The music data bloc is a flat command stream. `X` is the low nibble of the
opcode and encodes a count/parameter.

| Opcode | Size | Meaning |
|--------|------|---------|
| `$00` | 1 | **Frame wait** — 1/60 s (NTSC) or 1/50 s (PAL). |
| `$1X` + data | 1 + (X+1) | **PSG write** — X+1 bytes to PSG port. |
| `$2X` + data | 1 + 2(X+1) | **YM2612 port 0 write** — X+1 (register, value) pairs. |
| `$3X` + data | 1 + 2(X+1) | **YM2612 port 1 write** — X+1 (register, value) pairs. |
| `$4X` + data | 1 + (X+1) | **YM2612 key on/off** — X+1 bytes to key register (`$28`). |
| `$5X` + id | 2 | **PCM play** — see below. |
| `$7E` + addr | 4 | **Loop** — 24-bit address (relative to music data bloc start) to loop to. |
| `$7F` | 1 | **End** of music data. |

`$6X` and `$80`–`$FF` are **reserved** — never emit them.

### Timing model
There is no "wait N frames" opcode — only the single-frame `$00`. A wait
of k frames is k consecutive `$00` bytes. All timing is quantized to
frames (1/60 s NTSC, 1/50 s PAL), so VGM's 44100 Hz sample-accurate waits
must be **accumulated and rounded to whole frames** during conversion.
This is a key lossy step: sub-frame timing is lost.

### YM2612 register writes (`$2X` / `$3X`)
Port 0 (`$2X`) carries channels 1–3 and global registers; port 1 (`$3X`)
carries channels 4–6. Each write is a `(register, value)` pair and up to
16 pairs (X = 0..15) can be batched under one opcode. This maps directly
from YM2612 register writes produced by the chip-conversion stage.

### YM2612 key on/off (`$4X`)
The `$28` key register is broken out into its own opcode for compactness.
Each data byte is one value written to `$28` (channel + operator mask).
Batch up to 16.

### PSG writes (`$1X`)
Raw bytes to the PSG port, up to 16 per opcode. This converter has **no
PSG source data** (X68000 has no PSG), so `$1X` is normally never emitted.
Kept here for completeness / future use.

### PCM play (`$5X id`)
```
X = (priority & 0xC) | (channel & 0x3)
    channel  = X & 0x3   → 0..3 (up to 4 simultaneous PCM channels)
    priority = X & 0xC   → 0,4,8,12 (higher = higher priority)
id  = sample id (1..63); references the sample id table. id 0 = stop channel.
```
When a play command targets a busy channel, it only replaces the current
sample if its priority is **≥** the playing sample's priority; otherwise
it's ignored. For music (non-SFX) conversion, a consistent priority and
simple channel-allocation policy is fine — the conversion doc defines how
OKIM6258 playback events map onto these channels.

### Loop (`$7E`) and End (`$7F`)
- `$7E` + 24-bit address: jump target for looping, **relative to the start
  of the music data bloc**. Set from the VGM loop offset (translated into
  the emitted command stream's address space, not the VGM's).
- `$7F`: marks end of music data. Always the final command.

## Generation checklist

1. Emit ident `"XGM "`.
2. Build the sample table: for each converted PCM sample, record
   `offset/256` and `size/256`; fill unused entries with `FFFF / 0001`.
3. Concatenate padded (256-aligned) 8-bit signed samples into the sample
   bloc; compute SLEN = total / 256.
4. Set version `0x01` and flags (NTSC/PAL, GD3, single-track).
5. Emit the music command stream:
   - accumulate VGM waits, round to frames, emit `$00` runs,
   - emit YM2612 register writes as `$2X`/`$3X`, key events as `$4X`,
   - emit PCM triggers as `$5X id`,
   - emit `$7E` at the loop point if the source loops,
   - terminate with `$7F`.
6. Write MLEN (byte length of the command stream) before the music bloc.
7. Append GD3 tags if enabled.

## Post-check with xgmtool
The emitted `.xgm` should be run through SGDK's `xgmtool` to compile and
validate it. `xgmtool` is the reference consumer — if it accepts the file
and the compiled output plays correctly, the format is right. Treat
`xgmtool` acceptance as an integration-test gate (see testing guidelines).
