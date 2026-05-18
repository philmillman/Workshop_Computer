# PlantHolder — Computer Card

**PlantHolder** is a Workshop Computer card for the [Instruo Pocket
Scion](https://instruomodular.com/product/pocket-scion/). The name is a nod to
two ideas: the Scion’s origins in **plant biosonification**, and the
**Householder-reflection** feedback matrix at the heart of the Fairfield
Circuitry **Placeholder** (EB) reverb this card emulates.

The Computer is a USB MIDI host for the Scion, runs a stereo Placeholder-style
reverb on the Scion’s audio, and exposes two channels of v/Oct + gate for the
rest of your rack.

---

## Features

- **USB MIDI host** — plug the Pocket Scion into the Computer’s USB port.
- **2-channel CV/Gate** — MIDI channels **1** and **2** (route voices in the
  Scion companion app); v/Oct + gate with ±2-semitone pitch bend.
- **Reverb** — three independent delay lines with Householder cross-feedback
  and a fourth in-phase tone-filtered path (Placeholder EB, 12–160 ms).
- **Clock bridge** — Pulse In 1 → MIDI clock (24 PPQN) to the Scion; **one
  pulse per quarter note** (Turing Machine–style).
- **CV** — CV In 1 → DECAY, CV In 2 → SIZE.

---

## Board requirement

USB host mode requires **Rev1_1** or newer (Q2 2025+). LED 0 off = no host /
older board.

---

## Hardware connections

| Computer jack | Role |
|---------------|------|
| Audio In 1/2  | Scion stereo out |
| Audio Out 1/2 | Reverb L/R |
| CV Out 1 / Pulse Out 1 | v/Oct + gate — MIDI ch 1 |
| CV Out 2 / Pulse Out 2 | v/Oct + gate — MIDI ch 2 |
| Pulse In 1    | External clock → MIDI clock to Scion |
| CV In 1       | DECAY |
| CV In 2       | SIZE |

---

## Controls

Panel mapping follows the Placeholder EB. **MIX** stays on Main except in the
latched MOD page.

### Switch Up — SIZE / RATIO

| Knob | Function |
|------|----------|
| Main | MIX |
| X    | SIZE (12–160 ms) |
| Y    | RATIO |

### Switch Middle — DECAY / TONE

| Knob | Function |
|------|----------|
| Main | MIX |
| X    | DECAY |
| Y    | TONE |

### Switch Down (hold > 1 s) — MOD (LED 5)

Toggle with a 1 s hold on Down.

| Knob | Function |
|------|----------|
| Main | MOD depth |
| X    | MOD type |
| Y    | MOD LPF |

---

## Reverb topology

Per the [EB Specifications](https://cdn.shopify.com/s/files/1/0234/8231/files/EB_Specifications.pdf):

```
mono in ──[HPF]──► 3 delay lines (SIZE / RATIO)
                    Householder cross-FB + 4th TONE path
                              └──► stereo wet
```

Implementation: `placeholder_reverb.h` (`PlaceholderReverb`).

---

## LEDs

| LED | Indication |
|-----|------------|
| 0 | USB MIDI connected |
| 1 | MIDI activity |
| 2 | Gate ch 1 |
| 3 | Gate ch 2 |
| 4 | External clock active |
| 5 | MOD page latched |

---

## Building

```bash
cd releases/83_plant_holder
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make plant_holder -j4
```

Flash `plant_holder.uf2`.

---

## Credits

- Fairfield Circuitry Placeholder (EB) — reverb topology.
- [rppicomidi/usb_midi_host](https://github.com/rppicomidi/usb_midi_host).
- [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard) — Chris Johnson.
