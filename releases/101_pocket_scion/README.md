# Pocket Scion — Computer Card

A Computer card for the Music Thing Workshop System that connects to an
[Instruo Pocket Scion](https://instruomodular.com/product/pocket-scion/)
via USB MIDI host, applies a spatial echo effect to the Scion's audio output,
and converts MIDI notes to CV/Gate for driving other modules.

---

## Features

- **USB MIDI host** — the Computer becomes a USB host; plug the Pocket
  Scion directly into the Computer's USB port with a USB cable.
- **2-channel monophonic CV/Gate output** — independently configurable
  MIDI channels (A & B) with v/Oct + gate, including ±2-semitone pitch
  bend tracking.
- **Spatial echo** — 3-parallel-delay-line effect inspired by the Fairfield
  Circuitry Placeholder (EB topology).  Three delay taps drawn from the
  same buffer at golden-ratio time relationships (T, T×0.618, T×0.382) are
  panned to opposite sides of the stereo field, creating a wide, diffuse
  spatial image.
- **Clock input** — Pulse In 1 locks the primary delay time to external
  clock tempo (period auto-detected from rising edges).
- **CV modulation** — CV In 1 trims the delay time by ±50 ms.
- **Settings persistence** — MIDI channel assignments are saved to flash
  and restored on power-up.

---

## Board requirement

USB host mode requires **Rev1_1** or newer board hardware (Q2 2025+).
On older boards, USB host mode is not available and LED 0 will remain off.

---

## Hardware connections

| Computer jack | Role                              |
|---------------|-----------------------------------|
| Audio In 1    | Scion left audio output           |
| Audio In 2    | Scion right audio output          |
| Audio Out 1   | Spatial echo left                 |
| Audio Out 2   | Spatial echo right                |
| CV Out 1      | v/Oct pitch — MIDI channel A      |
| CV Out 2      | v/Oct pitch — MIDI channel B      |
| Pulse Out 1   | Gate — MIDI channel A             |
| Pulse Out 2   | Gate — MIDI channel B             |
| Pulse In 1    | External clock input              |
| CV In 1       | Delay time modulation (±50 ms)    |

---

## Controls

| Control     | Function                                                    |
|-------------|-------------------------------------------------------------|
| Main Knob   | Dry/wet mix                                                 |
| X Knob      | Time — primary tap delay (0–250 ms); locked to clock when   |
|             | a clock signal is present on Pulse In 1                     |
| Y Knob      | Feedback/regeneration (0–95 %)                              |
| Switch Up   | Wide stereo spread (tap 1 hard-L, tap 3 hard-R)             |
| Switch Mid  | Medium stereo spread                                        |
| Switch Down | Mono / narrow (all taps centred)                            |

---

## Spatial effect topology (Placeholder EB)

```
                         ┌──────────────────────────────────────────────┐
                         │                                              │
stereo in ──(L+R / 2)──► write ──► [16384-sample mono buffer, 32 KB]   │
                                      │           │           │         │
                                    tap 1       tap 2       tap 3       │
                                  T (long)    T × 0.618   T × 0.382     │
                                   Pan L       Pan Ctr     Pan R         │
                                      │           │           │         │
                                      └─────── sum / 3 ───────┘         │
                                                  │                      │
                                               [HPF] ──► × feedback ─────┘
```

The three taps at golden-ratio time relationships (φ^-1 ≈ 0.618,
φ^-2 ≈ 0.382) create a naturally non-repeating, diffuse echo pattern.
Panning the taps to different stereo positions places each echo "in
space" rather than simply time, giving the characteristic Placeholder
spatial quality.

### Memory budget

| Component           | RAM    |
|---------------------|--------|
| Spatial echo buffer | 32 KB  |
| Code + stack        | ~50 KB |
| **Total**           | **~82 KB** (of 256 KB available) |

---

## LED indicators (normal mode)

| LED | Indication                                        |
|-----|---------------------------------------------------|
| 0   | USB MIDI device connected                         |
| 1   | MIDI activity (brief flash on each message)       |
| 2   | Gate A (lit while note held on channel A)         |
| 3   | Gate B (lit while note held on channel B)         |
| 4   | Clock pulse (blinks with Pulse In 1)              |
| 5   | CV In 1 level (brightness follows CV value)       |

---

## MIDI channel selection

By default both channels are set to MIDI channel 1.

### Select Channel A
1. Move the switch to the **Up** position and hold it there for **2 seconds**.
2. LEDs 0–3 show the current channel number in 4-bit binary (0000 = ch 1 … 1111 = ch 16).  
   LED 4 is lit to indicate channel A is being edited.
3. Turn the **Main Knob** to scan channels 1–16.
4. Flick the switch to **Middle** to confirm and save, or to **Down** to
   confirm channel A and immediately proceed to select channel B.

### Select Channel B
1. Move the switch to the **Down** position and hold it there for **2 seconds**.
2. LEDs 0–3 show the current channel number. LED 5 is lit.
3. Turn the **Main Knob** to select the channel.
4. Flick the switch to **Middle** to confirm and save, or to **Up** to
   confirm channel B and immediately proceed to select channel A.

Both channels can be set to the same MIDI channel; in that case the last
received note wins on both CV/Gate outputs (last-note priority).

---

## Building

Requires [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
(2.0 or later) with TinyUSB updated to latest master.

### Update TinyUSB

```bash
cd $PICO_SDK_PATH/lib/tinyusb
git checkout master && git pull
```

### Build

```bash
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
make -j4
```

Flash `pocket_scion.uf2` onto your Computer card.

---

## Credits

- Spatial echo topology inspired by the Fairfield Circuitry Placeholder (EB revision).
- USB MIDI host driver from [rppicomidi/usb_midi_host](https://github.com/rppicomidi/usb_midi_host).
- [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard)
  hardware abstraction library by Chris Johnson.
