# Conversion Mapping — X68000 → Mega Drive VGM

The core specification. Defines exactly how the transcriber turns X68000
VGM contents (YM2151 + OKIM6258) into Mega Drive VGM contents (YM2612 +
DAC PCM). Read the chip docs first (`chips/ym2151.md`, `chips/ym2612.md`,
`chips/oki-m6258.md`); this doc assumes their register/bit detail and
focuses on the *mapping*.

Pipeline position:
```
X68000 VGM ──[THIS SPEC]──▶ Mega Drive VGM ──[xgmtool]──▶ XGM
```
Downstream, xgmtool handles VGM→XGM, PCM resampling to 14 kHz, frame-timing
quantization, and sample dedup. This spec produces a clean MD VGM; it does
**not** do any of xgmtool's jobs.

## 1. Overall structure

For each input command in the source VGM stream, in order:
- **YM2151 write** (`0x54 aa dd`) → translate to zero or more YM2612 writes
  (`0x52`/`0x53`), subject to the channel keep-list.
- **OKIM6258 activity** (`0xB7`, or DAC-stream `0x90`–`0x95` + type-`0x04`
  data blocks) → decode ADPCM to PCM, emit as YM2612 DAC data + stream.
- **Waits** (`0x61`/`0x62`/`0x63`/`0x7n`) → copy through unchanged (same
  44100 Hz sample base; xgmtool quantizes to frames later).
- **End** (`0x66`) → end the MD VGM stream.
- Everything else → drop (see §7).

The output MD VGM header sets the YM2612 clock (see §6), leaves SN76489 and
all other chip clocks at 0, and carries loop offset/samples across.

## 2. Channel allocation (keep-list + positional mapping)

The user supplies `--fm-channels`, an explicit keep-list of YM2151 channel
indices (0–7) to retain. Rules (validated up front):
- Indices in range 0–7, comma-separated, no duplicates.
- Max **6** channels, or max **5** if the source has PCM (OKIM6258 present),
  because YM2612 channel 6 is reserved for DAC. A 6-entry list with active
  PCM is an error (tell the user PCM forces a 5-channel max).
- Fewer than the max is allowed; unused YM2612 channels stay idle.

**Positional mapping.** Sort the keep-list ascending; map in order onto
YM2612 channels:
```
sorted_keep[0] -> YM2612 ch0
sorted_keep[1] -> YM2612 ch1
...
```
YM2612 channels 0–2 live on **port 0** (VGM `0x52`), channels 3–5 on
**port 1** (VGM `0x53`). With PCM active, only ch0–4 receive FM; ch5 stays
free and ch6 is DAC.

Maintain a **source→target channel table** (YM2151 channel → YM2612 channel
+ port, or "dropped"). Every per-channel and per-operator translation
consults it. Writes to a **dropped** YM2151 channel produce **no** output.

### Register addressing translation
YM2151 addresses operators as `op = reg & 0x1F` with layout
`channel + 8*slot`. YM2612 addresses per-channel registers as
`base + channel_in_port` and per-operator registers by nibble
(`0x0/0x4/0x8/0xC` for the 4 slots) within a port bank. The translator must:
1. Decode the YM2151 (channel, slot) from the source register.
2. Look up the target YM2612 channel + port (or drop).
3. Re-encode into the YM2612 register number and choose port 0/1.

## 3. FM register field mapping

Most fields copy directly (same bit layout); a few need transform. Source
register groups per `chips/ym2151.md`, targets per `chips/ym2612.md`.

| YM2151 (source) | YM2612 (target) | Transform |
|-----------------|-----------------|-----------|
| `0x20`–`0x27` RL/FB/CONNECT | `0xB0`–`0xB3` ALG/FB | Algorithm (0–7) & Feedback (0–7) copy directly. |
| `0x20`–`0x27` RL bits | `0xB4`–`0xB7` L/R | **Bit swap**: YM2151 R=bit7,L=bit6 → YM2612 L=bit7,R=bit6. Always enable at least one, or channel is silent. |
| `0x38`–`0x3F` PMS/AMS | `0xB4`–`0xB7` PMS/AMS | PMS (bit6-4→bit2-0), AMS (bit1-0→bit5-4). Repack into same `0xB4` write as L/R. |
| `0x40`–`0x5F` DT1/MUL | `0x30`–`0x3F` DET/MUL | Direct copy (bit6-4 DT1, bit3-0 MUL). |
| `0x60`–`0x7F` TL | `0x40`–`0x4F` TL | Direct copy (bit6-0). |
| `0x80`–`0x9F` KS/AR | `0x50`–`0x5F` RS/AR | Direct copy (bit7-6 KS, bit4-0 AR). |
| `0xA0`–`0xBF` AM/D1R | `0x60`–`0x6F` AM/DR | Direct copy (bit7 AM, bit4-0 rate). |
| `0xC0`–`0xDF` DT2/D2R | `0x70`–`0x7F` SR | **D2R copies** (bit4-0). **DT2 dropped** (no YM2612 detune-2). |
| `0xE0`–`0xFF` D1L/RR | `0x80`–`0x8F` SL/RR | Direct copy (bit7-4 SL, bit3-0 RR). |
| `0x28`–`0x2F` KC + `0x30`–`0x37` KF | `0xA4`/`0xA0` block+fnum | **Computed** — see §4. |
| `0x08` key on/off | `0x28` key on/off | **Re-encoded** — see §5. |
| SSG-EG | `0x90`–`0x9F` | Source has none; leave YM2612 default (0). |

**Note on `0xB4` packing.** YM2612 `0xB4` holds L/R **and** AMS/PMS in one
register. YM2151 splits these across `0x20` (L/R) and `0x38` (PMS/AMS). The
translator must **track both source fields per channel** and write the
combined YM2612 `0xB4` whenever either changes.

## 4. Pitch conversion (KC/KF → fnum/block) — VERIFIED

This is the one real computation. YM2151 pitch is note-based (Key Code +
Key Fraction); YM2612 is F-number + Block. The method below was verified
numerically: worst-case error **0.414 cents** across octaves 2–6 and all 12
notes (inaudible; the only loss is integer rounding of fnum).

### Step 1 — YM2151 (octave, note, fraction) → absolute frequency

Decode KC (`0x28`–`0x2F`, per channel): `octave = (KC>>4)&7`,
`note_code = KC&0xF`. Only 12 note codes are valid; map to a semitone
(0 = C):
```
note_code:  0  1  2   4  5  6   8  9 10  12 13 14
semitone :  0  1  2   3  4  5   6  7  8   9 10 11
            C  C# D    D# E F    F# G G#   A  A# B
```
(Codes with low 2 bits = 0b11, i.e. 3,7,11,15, are invalid note slots; if
one appears, clamp to the nearest valid code.)

KF (`0x30`–`0x37`, per channel): `frac = (KF>>2) & 0x3F` — 64 sub-steps per
semitone.

Absolute frequency, anchored to A4 and scaled by the **actual source
clock** (X68000 = 4 MHz; read from VGM header `0x30`):
```
semis_from_A4 = (octave - 4)*12 + (semitone - 9) + frac/64
f = 440.0 * 2^(semis_from_A4 / 12) * (clock_2151 / 3579545)
```
The `clock_2151/3579545` factor matters: at 4 MHz the X68000's pitch sits
~1.9 semitones above a standard-clock YM2151 (its A4 ≈ 491.7 Hz, not 440).
This is intentional — the music was composed for that hardware tuning — and
must be preserved by carrying the true output frequency across.

### Step 2 — absolute frequency → YM2612 fnum/block

Invert the YM2612 frequency formula (verified against A4/octave doubling):
```
f = fnum * clock_2612 / (144 * 2^(21 - block))
  ⇒ fnum = f * 144 * 2^(21 - block) / clock_2612
```
Choose **block** so fnum lands in the high-precision range **[1024, 2047]**
(the 11-bit fnum's top octave). Algorithm:
```
for block in 0..7:
    fnum = round( f * 144 * 2^(21-block) / clock_2612 )
    if 1024 <= fnum <= 2047: use this (block, fnum); stop
// if none fit (extreme pitch), clamp to the nearest achievable block/fnum
```
Emit as the YM2612 two-write latch (see `chips/ym2612.md`): write
`0xA4`/`0xA5…` (block + fnum high) **first**, then `0xA0…` (fnum low) to
commit. Use the correct port for the target channel.

### Reference values (X68000 4 MHz → MD NTSC 7670442 Hz)
```
A4 : 491.68 Hz → block 4, fnum 1210
C4 : 292.36 Hz → block 3, fnum 1439
C5 : 584.71 Hz → block 4, fnum 1439   (octave up: same fnum, block+1)
C2 :  73.09 Hz → block 1, fnum 1439
B6 : 2207.58 Hz → block 6, fnum 1358
```

### Pitch envelope / LFO note
KF changes over time (vibrato, pitch slides) produce new KC/KF writes; each
recomputes fnum/block via the same path. YM2151 hardware LFO PM (via PMS)
is carried as the PMS field (§3); it is not re-derived per-sample.

## 5. Key on/off re-encoding

YM2151 `0x08`: `bit2-0 = channel (0–7)`, operator-enable bits at
`bit6=C2, bit5=M2, bit4=C1, bit3=M1`.

YM2612 `0x28`: channel select is **bank-encoded** — `0,1,2` = ch1–3,
`4,5,6` = ch4–6 (values 3 and 7 invalid); operator mask in `bit7-4`.

Translation:
1. Read source channel; look up target YM2612 channel (or drop if not
   kept).
2. Encode the target channel select: for YM2612 ch0–2 → `0,1,2`; for ch3–5
   → `4,5,6`. **Never emit 3 or 7.**
3. Map the operator-enable bits to YM2612's `bit7-4` (op4,op3,op2,op1). The
   YM2151 slot→operator correspondence follows the same
   modulator/carrier slot order; map slot0→op1(bit4) … slot3→op4(bit7).
4. Emit `0x28 <encoded>` on port 0 (the `0x28` key register is global,
   always written via `0x52`).

Key events for **dropped** channels produce no output.

## 6. Output MD VGM header

- Identifier `"Vgm "`, version ≥ 1.50 (1.51+ recommended for data blocks).
- **YM2612 clock** (`0x2C`): use the standard Mega Drive NTSC value
  **7670442** (or the PAL value if the source `Rate` at `0x24` indicates
  PAL: 7600489). Emit consistently; the pitch math (§4) must use the same
  value chosen here.
- **YM2151 clock, OKIM6258 clock, SN76489 clock, all others**: 0.
- Carry across **loop offset** (`0x1C`) and **loop # samples** (`0x20`),
  translated to the emitted stream's positions.
- **Total # samples** (`0x18`): recompute from the emitted wait total.
- VGM data offset (`0x34`) set to the actual start of the emitted stream.

## 7. Lossy drops (documented, intentional)

These source features have no faithful YM2612 target and are dropped. This
is the "X68000 has more capability" loss the project accepts.

| Dropped | Why |
|---------|-----|
| YM2151 channels beyond the keep-list | User-selected 8→6 (or 8→5) reduction. |
| YM2151 **noise** (`0x0F`) | YM2612 has no noise generator. (SN76489 could in theory host noise, but there is no PSG source and the driver reserves it — out of scope.) |
| YM2151 **DT2** (detune 2) | YM2612 operators have no DT2. |
| Timers, CSM, IRQ (`0x10`–`0x14`) | No musical content; playback is VGM-timed. |
| CT1/CT2 output pins (`0x1B` bit7-6) | External control lines, not audio. |
| Sub-frame timing | Not dropped here — **xgmtool** quantizes to frames downstream. Pass waits through unchanged. |

Log a one-line summary at the end (counts of dropped channels, whether
noise/DT2 were present) so the user knows what was lost.

## 8. PCM path (OKIM6258 → YM2612 DAC)

Per `chips/oki-m6258.md` (Option P1 — decode in the tool):

1. **Detect** OKIM6258 usage: direct `0xB7` register writes and/or
   DAC-stream control (`0x90`–`0x95`) referencing type-`0x04` data blocks.
   Support both source paths.
2. **Reconstruct** the ADPCM byte stream for each played sample.
3. **Decode** 4-bit ADPCM → linear PCM using the verified XM6 algorithm
   (step table `1.1^step`, `index_shift`, leaky-integrator
   `(sample<<8 + signal*245)>>8`, per-sample state reset `signal=-2,
   step=0`). **Down-convert to 8-bit unsigned** (`(sample>>6)+0x80`, i.e.
   the 10-bit signal mapped to 8-bit, centred at `0x80`); do **not**
   resample.
   > **Deviation from the original Option P1 (16-bit, no down-convert).**
   > The xgmtool we build (1.x) has **no 16-bit sample path**: its
   > `resample()` reads the data block as 8-bit unsigned (`data[i]&0xFF -
   > 0x80`) and its DAC-stream setup is `0x90 id 02 00 2A`. Handing it
   > 16-bit data yields garbled, double-length audio. So the tool emits
   > 8-bit unsigned here and still lets xgmtool own the **resampling** to
   > 14 kHz.
4. **Determine native sample rate** from clock (`0x90`) + divider (`0x94`)
   with the half-rate correction: `sample_rate = clock / (divider*2)`
   (rounded). Tag the emitted PCM with this rate.
5. **Emit into the MD VGM** as a **YM2612 PCM data block (type `0x00`)**
   plus **DAC-stream control commands** (`0x90`–`0x95`) that play it on
   channel 6, and set **DAC enable** (YM2612 `0x2B` bit7) so ch6 is the DAC.
   This is the exact shape xgmtool's sample extractor reads.
6. Let **xgmtool** resample to 14 kHz and format for the XGM DAC
   downstream. The tool does neither.

Channel-6 reservation (§2) is what makes room for this: with PCM present,
the keep-list maxes at 5 and YM2612 ch6 carries DAC only.

## 9. Validation hook

After producing the MD VGM, the intermediate can be sanity-checked with
`vgm2txt` (YM2612 writes are human-readable, with pitch info) before it's
fed to xgmtool. Round-trip acceptance by xgmtool (produces a valid `.xgm`
that plays) is the integration gate — see `testing-guidelines.md`.

## 10. Order-of-operations summary

1. Parse source header; read clocks (2151 `0x30`, OKI `0x90`/`0x94`), loop.
2. Validate `--fm-channels`; build source→target channel table (apply PCM
   ch6 reservation).
3. Walk source stream in order:
   - YM2151 writes → translate fields (§3), pitch (§4), key (§5) through the
     channel table.
   - OKIM6258 activity → decode + buffer PCM (§8).
   - waits → copy; end → finish.
4. Emit MD VGM: header (§6), FM command stream, PCM data block + DAC stream
   (§8), loop points, recomputed totals.
5. Emit drop summary (§7).
