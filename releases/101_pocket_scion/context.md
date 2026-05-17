# Session Context — Pocket Scion spatial-effect refactor

## What was done

The original `delay.h` + `reverb_dsp.c/h` series FX chain was replaced with
a single spatial echo unit inspired by the **Fairfield Circuitry Placeholder
(EB revision) 3-parallel-delay-line topology**.

### New file: `placeholder_spatial.h`

Header-only `PlaceholderSpatial` class.

**Signal path:**
```
stereo in ──(L+R / 2)──► write ──► [16384-sample int16 circular buffer, 32 KB]
                                       │            │            │
                                     tap 1        tap 2        tap 3
                                   T (long)    T × 0.618    T × 0.382
                                    Pan L       Pan Ctr      Pan R
                                       │            │            │
                                       └─────── sum / 3 ────────┘
                                                    │
                                                 [HPF] ──► × feedback ──► back to write
```

- Buffer: `int16_t buf_[16384]` (32 KB, power-of-2 for bitmask addressing)
- Tap times: primary T, then T×(618/1000) and T×(382/1000) — golden ratio φ⁻¹, φ⁻²
- Stereo panning: `panA = 4096 + spread`, `panB = 4096 − spread`
  - spread=0 → mono; spread=4096 → tap1 hard-L, tap3 hard-R
- Feedback: mixed tap sum → 1-pole HPF (fc ≈ 32 Hz, α = 32639/32768) → scaled by `feedback_`
- Primary tap slewed ±1 sample/call to avoid clicks on tempo change
- All arithmetic: `int32_t` (fast on RP2040 Cortex-M0+)
- No heap allocation — fits in BSS alongside the rest of `g_card`

**API:**
```cpp
PlaceholderSpatial fx;
fx.setTime(int32_t samples);       // 2 .. TIME_MAX (12000 = 250 ms @ 48 kHz)
fx.setFeedback(int32_t fb);        // 0 .. 31130 (≈ 95 %)
fx.setSpread(int32_t spr);         // 0 .. 4096
fx.process(inL, inR, outL, outR);  // ±2048 in/out
```

---

### Changes to `pocket_scion.cpp`

| Item | Before | After |
|------|--------|-------|
| Includes | `delay.h`, `reverb_dsp.h` | `placeholder_spatial.h` |
| Global | `static reverb *g_reverb` | removed |
| `main()` | `g_reverb = reverb_create()` | removed |
| Member | `StereoDelay delay_` | `PlaceholderSpatial fx_` |
| X Knob | Reverb size/decay | **Time** (0–250 ms) |
| Y Knob | Delay feedback | **Feedback/regen** (0–95 %) |
| Switch Up | Reverb-heavy blend | **Wide stereo spread** (tap1 L, tap3 R) |
| Switch Mid | Balanced blend | **Medium spread** |
| Switch Down | Delay-heavy blend | **Mono / narrow** |
| CV In 1 | Target selected by switch | Always modulates time ±50 ms |
| Clock lock | Overrides delay time | Overrides primary tap time (same logic) |

FX step in `ProcessSample()` is now:
```cpp
fx_.setTime(timeSamples);
fx_.setFeedback(feedbackLevel);
fx_.setSpread(spread);
int32_t wetL, wetR;
fx_.process(inL, inR, wetL, wetR);
int32_t outL = ((inL * dryLevel) + (wetL * wetLevel)) >> 12;
int32_t outR = ((inR * dryLevel) + (wetR * wetLevel)) >> 12;
AudioOut1((int16_t)outL);
AudioOut2((int16_t)outR);
```

---

### Changes to `CMakeLists.txt`

`reverb_dsp.c` removed from `target_sources`. Build is otherwise identical.

---

### Incidental pre-existing bug fixes (in copied driver files)

| File | Bug | Fix |
|------|-----|-----|
| `reverb_dsp.c` line 47 | `memset(db, 0, sizeof(delay))` — zeroed `uint16_t`-sized bytes instead of the struct | `sizeof(*db)` |
| `usb_midi_host.c` line 65 | Duplicate `;` on struct member `baAssocJackID[]` | Removed extra `;` |
| `usb_midi_host.h` line 48 | Typo `exampe` | `example` |
| `info.yaml` | Stale description mentioning "delay & reverb" | Updated to "spatial echo effect" |

---

## RAM budget after change

| Component | Before | After |
|-----------|--------|-------|
| Reverb heap | ~80 KB | 0 KB (removed) |
| Delay buffer (StereoDelay) | 48 KB (2× int16[12000]) | 0 KB |
| PlaceholderSpatial buffer | 0 KB | 32 KB (int16[16384]) |
| **Net saving** | | **~96 KB** |

Total RAM used: ~82 KB of 264 KB available.

---

## Possible next steps / things to consider

1. **Tap time relationships** — currently hard-coded as exact golden-ratio fractions
   (618/1000 and 382/1000).  The real Placeholder uses slightly detuned ratios
   to reduce comb-filter artefacts; you may want to experiment with small offsets
   (e.g. ×0.610 and ×0.374).

2. **Feedback character** — the current HPF on the feedback path has a very low
   cutoff (~32 Hz).  Raising it (e.g. to 200 Hz) would give a thinner, brighter
   decay reminiscent of the physical pedal.

3. **Input summing** — the buffer is written from `(inL + inR) / 2`.  If you
   want each channel to retain more identity in the echoes, consider writing two
   separate buffers (doubles RAM but enables true stereo echo).

4. **Wet signal saturation** — the output panning arithmetic can theoretically
   slightly exceed ±2048 when `spread_ = 4096` and all three taps are at full
   scale simultaneously.  Adding soft-clipping before `AudioOut1/2` would prevent
   any headroom issues.

5. **Clock division** — the clock logic currently maps pulse-period directly to
   tap-1 time.  The original code divided by 4 to get quarter-note subdivisions;
   you could re-introduce a selectable clock division (÷1, ÷2, ÷4) via the
   switch or another CV input.

6. **Build & flash** — no toolchain changes needed; standard Pico SDK build:
   ```bash
   mkdir build && cd build
   cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
   make pocket_scion -j4
   # → pocket_scion.uf2
   ```

---

## Branch

`copilot/create-implementation-plan-computer-card`
