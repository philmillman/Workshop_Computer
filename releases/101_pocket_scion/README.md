# Pocket Scion — Computer Card

A Computer card for the Music Thing Workshop System that connects to an
[Instruo Pocket Scion](https://instruomodular.com/product/pocket-scion/)
via USB MIDI host, processes the Scion's audio output, and converts MIDI
notes to CV/Gate for driving other modules.

---

## Features

- **USB MIDI host** — the Computer becomes a USB host; plug the Pocket
  Scion directly into the Computer's USB port with a USB cable.
- **2-channel monophonic CV/Gate output** — independently configurable
  MIDI channels (A & B) with v/Oct + gate, including ±2-semitone pitch
  bend tracking.
- **Stereo audio processing** — delay and plate reverb in series, applied
  to the Scion's stereo audio output.
- **Clock input** — Pulse In 1 locks the delay time to external clock tempo
  (4 pulses/beat assumed; period auto-detected).
- **CV modulation of FX** — CV In 1 modulates a selectable FX parameter
  depending on switch position.
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
| Audio Out 1   | Processed audio left              |
| Audio Out 2   | Processed audio right             |
| CV Out 1      | v/Oct pitch — MIDI channel A      |
| CV Out 2      | v/Oct pitch — MIDI channel B      |
| Pulse Out 1   | Gate — MIDI channel A             |
| Pulse Out 2   | Gate — MIDI channel B             |
| Pulse In 1    | External clock input (4 ppb)      |
| CV In 1       | FX parameter modulation           |

---

## Controls

| Control     | Function                                              |
|-------------|-------------------------------------------------------|
| Main Knob   | Dry/wet mix (or delay time when no clock present)     |
| X Knob      | Reverb size / decay                                   |
| Y Knob      | Delay feedback (0–95 %)                               |
| Switch Up   | Reverb-heavy blend; CV In 1 → reverb size             |
| Switch Mid  | Balanced delay + reverb; CV In 1 → delay time         |
| Switch Down | Delay-heavy blend; CV In 1 → dry/wet mix              |

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

## Audio signal path

```
Scion audio out → [Computer Audio In] → Delay → Reverb → [Computer Audio Out]
```

The delay and reverb are in series: the delay output feeds the reverb input.
This gives well-diffused reverb tails on the repeats.

| Parameter     | Range       |
|---------------|-------------|
| Delay time    | 1 ms – 250 ms (or clock-locked up to 250 ms) |
| Delay feedback | 0 – 95 %   |
| Reverb        | Dattorro plate algorithm, integer arithmetic |

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

- Reverb DSP adapted from `releases/20_reverb/` (Dattorro plate algorithm)
  by Chris Johnson.
- USB MIDI host driver from [rppicomidi/usb_midi_host](https://github.com/rppicomidi/usb_midi_host).
- [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard)
  hardware abstraction library by Chris Johnson.
