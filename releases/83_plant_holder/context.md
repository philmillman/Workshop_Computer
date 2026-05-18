# Session Context — PlantHolder (`83_plant_holder`)

Card formerly developed as `101_pocket_scion` / “Pocket Scion”. Renamed **PlantHolder**:
a pun on the Placeholder pedal and the Scion’s plant-biosonification heritage.

---

## Current firmware summary

| Area | Detail |
|------|--------|
| **Target** | `plant_holder.uf2` from `plant_holder.cpp` |
| **Class** | `PlantHolder` : `ComputerCard` |
| **DSP** | `placeholder_reverb.h` — `PlaceholderReverb` (EB FDN) |
| **MIDI in** | USB host; notes/pitch bend on **ch 1 → CV A**, **ch 2 → CV B** (hardcoded) |
| **MIDI out** | Pulse In 1 → Start/Stop + 24 PPQN clock to Scion |
| **Reverb** | Not clock-synced; SIZE/RATIO/DECAY/TONE/MOD via knobs + CV |
| **Flash settings** | Removed (channels fixed; use Scion app) |

---

## Reverb (`placeholder_reverb.h`)

Digital Placeholder (EB) per [EB_Specifications.pdf](https://cdn.shopify.com/s/files/1/0234/8231/files/EB_Specifications.pdf):

- **3×** independent `int16[8192]` delay lines (~48 KB)
- **Householder-style cross-feedback** — each line fed by the other two only; fourth in-phase path through **TONE** tilt on wet sum
- **SIZE** 12–160 ms; **RATIO** scales lines 2 & 3
- **MOD** — cyclical / random / both; rate ∝ 1/delay; optional mod LPF (2k–open)
- Stereo wet: sum + width from d1 − d3

```cpp
PlaceholderReverb reverb_;
reverb_.setTime(samples);
reverb_.setRatio(ratio);
reverb_.setFeedback(decay);
reverb_.setTone(tone);
reverb_.setModDepth / setModType / setModLpCutoff(...);
reverb_.process(inL, inR, wetL, wetR);
```

---

## Control pages (switch + MOD latch)

| Page | Main | X | Y |
|------|------|---|---|
| **Up** | MIX | SIZE | RATIO |
| **Mid** | MIX | DECAY | TONE |
| **Down hold >1s** (LED 5 latch) | MOD depth | MOD type | MOD LPF |

- MIX stored while editing MOD; return to Up/Mid to change mix.
- **No** MIDI channel UI (removed); **no** long-hold channel select on Up/Mid.

---

## CV & clock

| Jack | Role |
|------|------|
| CV In 1 | DECAY trim (bipolar on knob value) |
| CV In 2 | SIZE trim |
| Pulse In 1 | **1 pulse = 1 quarter note** → measure period → emit MIDI clock @ 24 PPQN to Scion; Start on lock, Stop after 2 s idle |

Reverb SIZE is **not** tied to Pulse In 1 (unlike early `101` drafts).

---

## MIDI / USB

- **Core 0** — 48 kHz audio, `PlantHolder::ProcessSample`, clock measurement
- **Core 1** — `tuh_task()`, `processMidiClockOut()`, RX parse
- **Rev1_1+** required for USB host
- Copied driver fixes: `usb_midi_host.c` (`baAssocJackID`); `usb_midi_host.h` (typo)

---

## History (chronological)

1. **Original** — `delay.h` + `reverb_dsp.c/h` series chain (~80 KB heap + 48 KB delay).
2. **Placeholder refactor** — single `PlaceholderSpatial` / golden-ratio taps from one buffer (incorrect topology).
3. **EB FDN** — three buffers, Householder cross-FB + global TONE path; renamed `PlaceholderReverb`.
4. **Controls** — switch pages map to MIX/SIZE/RATIO, DECAY/TONE, MOD; CV1/CV2 = DECAY/SIZE.
5. **MIDI cleanup** — fixed ch 1/2; flash channel UI removed; Pulse In → MIDI clock only (not reverb).
6. **Clock** — 4 PPQN assumption dropped; **1 pulse per quarter** (Turing Machine convention).
7. **Copy** — “spatial echo” → **reverb** in docs; internal rename to `placeholder_reverb.h`.
8. **Rename** — release folder `83_plant_holder`, product name **PlantHolder**.

---

## RAM (~98 KB of 256 KB)

| Component | Size |
|-----------|------|
| Reverb 3×8192 | 48 KB |
| Code + stack | ~50 KB |

---

## Build

```bash
cd Workshop_Computer/releases/83_plant_holder
mkdir build && cd build
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH ..
make plant_holder -j4
```

`usb_midi_host.c` maps legacy `USBH_EPSIZE_BULK_MAX` to `TUH_EPSIZE_BULK_MPS` for Pico SDK TinyUSB.

---

## Possible follow-ups

- Selectable clock division (÷1/÷2/÷4) on MOD page for non–quarter-note clocks
- Persist nothing, or optional Scion-side-only settings
- Soft-clip wet output at extreme spread/mod
- Slight ratio detune vs pedal to reduce combs

---

## Branch (historical)

`card/83_plant_holder`
