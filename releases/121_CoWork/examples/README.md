# Example content

Ready-made test material for the web manager (`web/cowork.html`). Everything
here is synthesized by [`generate.mjs`](generate.mjs) (`node examples/generate.mjs`),
so it's license-clean and reproducible.

## Songs (`midi/`)

Drop onto a song slot; the channel mapping is auto-suggested
(ch1 → Lead, ch2 → Bass, ch3 → Pad, ch10 → Drums).

| File | What it tests |
|---|---|
| `01_first_test.mid` | 2 bars, 120 BPM — simplest smoke test: 4-on-floor kick, snare 2+4, 8th hats, quarter-note bass, 8th-note lead arp, one pad chord per bar |
| `02_groove.mid` | 4 bars, 100 BPM — velocity dynamics, ghost notes, syncopated bass, sustained pad chords, open-hat chokes against closed hats, tom fill into the loop |
| `03_tempo_ride.mid` | 8 bars accelerating 90→132 BPM — exercises the tempo map (leader follows it; a follower slaves to the leader's clock through the changes) |
| `04_long_haul.mid` | 64 bars, ~2:20 at 112 BPM — a full arrangement (intro, verses, choruses, break, build, outro with a final ritardando) for long-run stability: sustained clock lock on a linked pair, a ~2700-event stream, section dynamics, crashes and fills |

## Samples (`samples/`)

16-bit mono 24 kHz WAV — already in the card's native format (the manager
accepts any format and converts, so your own WAV/MP3/AIFF work too).

Instruments (melodic mode): `lead_c4.wav` (root C4), `bass_c2.wav`
(root C2) and `pad_c4.wav` (root C4) — the defaults for each slot. All
three sustain steadily, so the **loop** toggle works on them. The pad
plays the 3-voice chord part (MIDI channel 3 in the songs).

Drum kit: drop each file onto the pad of the same name — the default GM
note map lines up with the songs above:

| Pad | File | Pad | File |
|---|---|---|---|
| 1 Kick | `kick.wav` | 9 Mid Tom | `tom_mid.wav` |
| 2 Rim | `rim.wav` | 10 Hi Tom | `tom_hi.wav` |
| 3 Snare | `snare.wav` | 11 Crash | `crash.wav` |
| 4 Clap | `clap.wav` | 12 Ride | `ride.wav` |
| 5 ClHat | `hat_closed.wav` | 13 Tamb | `tambourine.wav` |
| 6 PdHat | `hat_pedal.wav` | 14 Cowbell | `cowbell.wav` |
| 7 OpHat | `hat_open.wav` | 15 Shaker | `shaker.wav` |
| 8 LowTom | `tom_low.wav` | 16 Clave | `clave.wav` |

The three hats share choke group 1 by default, so `02_groove.mid`'s closed
hats cut the open hat — listen for it.

## Quick start

1. Connect the card, open the manager, load `01_first_test.mid` into slot 1.
2. Drop the whole `samples/` folder anywhere on the page — every file
   auto-maps to the right slot (instruments included, with their root
   notes read from the file names). Confirm the dialog.
3. **Apply**, then tap the switch (or the manager's Play). Melodic mode
   plays lead+pad+bass; hold the switch ~1 s to hear the drum side, and
   double-tap it to jump to the next loaded song.
4. Try the Main knob while playing: it loop-rolls the sequence (from a
   16th-note ratchet up to 8 bars); fully clockwise resumes normal
   playback in time.
