# CoWork

**A dual-module sample sequencer for the Workshop System Computer.**

CoWork turns one or two Computers into a sample groovebox driven by your own
MIDI files and sounds. One module plays **melodic** parts (a duophonic lead,
a 3-voice chord pad and a mono bass, with v/oct + gate outputs); the other
plays **percussive** parts (a 16-pad drum kit with trigger outputs). Link the two front-panel USB
ports and one module leads — sending MIDI clock, start and stop — while the
other follows, sample-locked. Each module also works entirely on its own.

Songs come from Standard MIDI Files and sounds from your own audio files,
prepared and uploaded in the browser — no SD card, no DAW needed at the gig.
Works on 2 MB program cards, shines on 16 MB.

## Setup

1. Flash [`UF2/cowork.uf2`](UF2/cowork.uf2) to a program card (hold BOOTSEL /
   use the site's web flasher). One binary serves 2 MB and 16 MB cards — the
   card size is detected at boot (2 MB: 4 songs + ~32 s of samples; 16 MB:
   8 songs + ~5.5 minutes).
2. Connect the Computer's USB port to your computer and open
   `web/cowork.html` (or the hosted editor) in Chrome/Edge. Click **Connect**.
3. Drop a `.mid` file on a song slot, drop samples on the instrument/drum
   pads, and **Apply**. Ready-made test songs and a full synthesized kit
   live in [examples/](examples/) — see its README for a quick start.

## Leader / follower

Set each module's Z switch **before power-up**:

| Z switch at boot | Role |
|---|---|
| Up | **Leader** — runs the clock, sends transport + clock over USB |
| Middle | **Follower** — locks to received clock (from the other module or any DAW/USB MIDI clock source) |

Connect the two modules' front-panel USB ports with a USB-C cable. Which
module is the electrical USB *host* is negotiated by the cable — it doesn't
matter, and it's independent of leader/follower. Typical setup: one module in
melodic mode (leader), the other in percussive mode (follower), both loaded
with the same song.

Notes:
- USB host mode needs a 2025 (rev 1.1) Computer. Older boards work as
  followers/devices only — the *other* module must then be the rev 1.1 one.
- The USB port role is settled when power is first applied; if the link
  doesn't come up after re-cabling, power-cycle both modules.
- A follower with no clock source can be started on its internal clock with
  a short press down on the switch.

## Controls

| Control | Function |
|---|---|
| Z switch at boot | Up = leader, Middle = follower |
| Z tap (short press down) | Transport start/stop (leader); internal-clock start/stop (follower without clock) |
| Z double tap | Next loaded song, skipping empty slots (lands at the loop point while running) |
| Z hold ~1 s | Toggle melodic / percussive engine |
| Main knob | **Loop roll**: loops the playing sequence — CCW to CW: 1 beat, 2 beats, 1 bar, 2 bars, 4 bars, 8 bars; full CW = no looping. The song position (and the leader's clock out) keeps running underneath, so turning it back CW drops you where the song would have been |
| X knob | Tempo 40–240 BPM (leader; pickup — the song's own tempo until moved). Follower: internal fallback tempo |
| Y knob | Master volume |

## Patching

| Jack | Melodic mode | Percussive mode |
|---|---|---|
| Audio out 1 | Lead + pad mix | Full kit mix |
| Audio out 2 | Bass mix | Lane submix (default: kick, configurable) |
| CV out 1 | Lead v/oct (calibrated) | Accent (last-hit velocity) |
| CV out 2 | Bass v/oct (calibrated) | Velocity of a chosen lane |
| Pulse out 1 | Lead gate | Kick trigger (10 ms, lane configurable) |
| Pulse out 2 | Bass gate | Snare trigger (10 ms, lane configurable) |
| CV in 1/2 | Forwarded to the peer module over USB (14-bit MIDI CC) | same |
| Pulse in 1/2 | Forwarded to the peer as triggers | same |
| Audio in | reserved (future FX) | same |

Each CV/pulse **output** can be switched (in the web editor) between its
local derivation and the **peer's forwarded inputs** — so a CV patched into
module A can modulate something patched from module B.

## LEDs

| LED | Meaning |
|---|---|
| Top-left | Role: solid = leader · slow blink = follower locked · fast blink = follower waiting · double-blink = both modules set to leader |
| Top-right | Link up (flickers during uploads) |
| Mid-left | Engine: off = melodic, on = percussive |
| Mid-right | Transport running |
| Bottom-left | Beat (bright on the downbeat) |
| Bottom-right | Voice activity (solid = CPU overrun latch, debug aid) |

## Web editor

`web/cowork.html` — Chromium browsers (Web Serial). Everything is staged
locally and written to the card in one **Apply** step (the transport is
stopped automatically; uploads are CRC32-verified both ways). Configuration
changes (routing, releases, note maps) apply live.

- **Songs**: drop `.mid` (format 0/1). Map each MIDI channel to Lead, Bass,
  Pad, Drums, or Ignore — channel 10 is auto-suggested as drums. Tempo maps
  are preserved. One slot holds ~8000 events.
- **Instruments**: lead, bass and pad each take one sample with a root note
  and optional sustain loop.
- **Drum kit**: 16 pads, GM-style default note map (editable), choke groups
  for open/closed hats.
- Samples are converted to 16-bit mono 24 kHz, trimmed and normalized.
- No hardware handy? Open with `?mock=1` for a simulated card.

## Technical notes

- 8-voice sample engine at 48 kHz on core 0 (192 MHz), int32 fixed-point;
  melodic mode allocates a duophonic lead, 3-voice pad and mono bass
  (stealing within each part), percussive plays 8-voice one-shots.
- Sequences play from flash as pre-baked 96 PPQN event streams — the browser
  converts SMF; the firmware never parses `.mid`.
- Core 1 owns USB (TinyUSB dual-role: composite MIDI+CDC device, or MIDI
  host via rppicomidi's driver) and all flash writes, gated by a
  quiesce handshake with the audio core.
- The follower locks to MIDI clock with a small software DLL (±3% rate trim,
  outlier rejection, hard resync past one beat) sized for USB framing jitter.
- Formats and the serial protocol are documented in
  [docs/FORMATS.md](docs/FORMATS.md); design notes for maintainers in
  [context.md](context.md).
- Host-side tests: `cd sim && ./build.sh` (engine/sequencer/clock DLL) and
  `node --test web/test/run_tests.mjs` (SMF converter, codecs, protocol
  against a mock card).

## Building

Pico SDK 2.2.0 / TinyUSB 0.20:

```
cmake -S . -B build && cmake --build build
```

or use the repo devcontainer and `make releases/121_CoWork` from the repo
root. The build fails deliberately if the binary outgrows its 256 KB flash
reserve (the rest of the card belongs to your songs and samples).

## License

MIT. The vendored `usb_midi_host` driver is MIT (rppicomidi);
`ComputerCard.h` is MIT (Chris Johnson).
