# Example content

Ready-made test material for the web manager (`web/cowork.html`). Everything
here is synthesized by [`generate.mjs`](generate.mjs) (`node examples/generate.mjs`),
so it's license-clean and reproducible.

## Songs (`midi/`)

Drop onto a song slot; the channel mapping is auto-suggested
(ch1 → Lead, ch2 → Bass, ch10 → Drums).

| File | What it tests |
|---|---|
| `01_first_test.mid` | 2 bars, 120 BPM — simplest smoke test: 4-on-floor kick, snare 2+4, 8th hats, quarter-note bass, 8th-note lead arp |
| `02_groove.mid` | 4 bars, 100 BPM — velocity dynamics, ghost notes, syncopated bass, open-hat chokes against closed hats, tom fill into the loop |
| `03_tempo_ride.mid` | 8 bars accelerating 90→132 BPM — exercises the tempo map (leader follows it; a follower slaves to the leader's clock through the changes) |

## Samples (`samples/`)

16-bit mono 24 kHz WAV — already in the card's native format (the manager
accepts any format and converts, so your own WAV/MP3/AIFF work too).

Instruments (melodic mode): `lead_c4.wav` (root C4 — the default) and
`bass_c2.wav` (root C2 — the default). Both sustain steadily, so the
**loop** toggle works on them.

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
2. Drop `lead_c4.wav` and `bass_c2.wav` on LEAD/BASS, `kick.wav`,
   `snare.wav`, `hat_closed.wav` on pads 1/3/5.
3. **Apply**, then short-press the switch (or the manager's Play). Melodic
   mode plays the lead+bass; hold the switch ~1 s to hear the drum side.
