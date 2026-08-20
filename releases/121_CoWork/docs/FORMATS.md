# CoWork — binary formats and protocol contract

This file is the single source of truth shared by the firmware (`src/`) and the
web manager (`web/cowork.html`). If you change anything here, change both sides
and bump the relevant `version` field.

Conventions: all multi-byte integers are **little-endian**. All flash offsets
are absolute (bytes from the start of flash, i.e. XIP_BASE-relative). All
writable regions are 4096-byte-sector granular. CRC32 = IEEE 802.3
(poly `0xEDB88320`, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`).

## 1. Flash memory map

Computed at boot from the JEDEC-detected flash size and **published over the
CDC protocol** (`I` command). The web UI must never hardcode offsets.

| Region            | Offset                | 2 MB card       | 16 MB card       |
|-------------------|-----------------------|-----------------|------------------|
| Firmware          | `0x000000`            | 256 KB reserve  | same             |
| Config sector     | `0x040000`            | 4 KB            | same             |
| Song slots        | `0x041000`            | 4 × 64 KB       | 8 × 64 KB        |
| Sample bank       | after songs           | dir + ~1.5 MB   | dir + ~15.2 MB   |

- Song slot count: 8 if detected flash ≥ 8 MB, else 4.
- Sample bank base = `0x041000 + slots * 0x10000`
  (2 MB: `0x081000`; 16 MB: `0x0C1000`).
- Sample bank layout: one 4 KB **directory sector** at the bank base, then
  sample data from `base + 0x1000` to the end of flash.
- Firmware reserve is enforced at build time (`cmake/check_firmware_reserve.cmake`).

## 2. Song slot format (`CWSQ`)

One slot = 64-byte header + `event_count` × 8-byte events, at the slot base.
Sequencer resolution is **96 PPQN** (exactly 4 × the 24 PPQN MIDI clock).

### Header (64 bytes)

| Off | Size | Field              | Notes                                        |
|-----|------|--------------------|----------------------------------------------|
| 0   | 4    | magic              | `"CWSQ"` = u32 `0x51535743`                   |
| 4   | 2    | version            | 1                                             |
| 6   | 2    | flags              | bit0 = loop enabled                           |
| 8   | 2    | ppqn               | 96                                            |
| 10  | 1    | tsig_num           | e.g. 4                                        |
| 11  | 1    | tsig_denom_log2    | e.g. 2 (= quarter note)                       |
| 12  | 4    | init_tempo_uspq    | µs per quarter note (500000 = 120 BPM)        |
| 16  | 4    | length_ticks       | bar-rounded song length                       |
| 20  | 4    | loop_start_tick    |                                               |
| 24  | 4    | loop_end_tick      | ≤ length_ticks                                |
| 28  | 4    | event_count        | includes the END sentinel                     |
| 32  | 24   | name[24]           | UTF-8, NUL padded                             |
| 56  | 4    | events_crc32       | CRC32 over `event_count * 8` event bytes      |
| 60  | 4    | reserved           | `0xFFFFFFFF`                                  |

Empty slot = erased flash (magic reads `0xFFFFFFFF`).

### Event (8 bytes), sorted by tick ascending

| Off | Size | Field | Notes                                                       |
|-----|------|-------|--------------------------------------------------------------|
| 0   | 4    | tick  | absolute, 96 PPQN, non-decreasing                            |
| 4   | 1    | type  | `0x00` NOTE_OFF, `0x01` NOTE_ON, `0x02` TEMPO, `0x7F` END    |
| 5   | 1    | part  | `0` lead, `1` bass, `2` drums, `3` pad — TEMPO: µs/qn bits 23..16 |
| 6   | 1    | d1    | MIDI note 0–127 — TEMPO: µs/qn bits 15..8                    |
| 7   | 1    | d2    | velocity 1–127 (ON) / 0 (OFF) — TEMPO: µs/qn bits 7..0       |

Tie-break order at equal tick: TEMPO → NOTE_OFF → NOTE_ON. The encoder
appends one END sentinel at `tick = length_ticks`. Drum events keep their
MIDI note in `d1`; the firmware maps note → drum slot via the sample
directory's `assign_note` fields at load/rescan time.

## 3. Sample bank format (`CWSB`)

Samples are **16-bit signed mono PCM at 24000 Hz** (the web UI converts).
The `sample_rate` field exists for future rates; v1 firmware requires 24000.

**The web UI is the allocator**: it computes 4 KB-aligned offsets (first-fit),
uploads only changed sample data, and rewrites the directory sector last.
The firmware only validates and plays.

### Directory sector (4096 bytes at bank base) — header (32 bytes)

| Off | Size | Field           | Value                                     |
|-----|------|-----------------|-------------------------------------------|
| 0   | 4    | magic           | `"CWSB"` = u32 `0x42535743`                |
| 4   | 2    | version         | 2                                          |
| 6   | 2    | slot_count      | 19                                         |
| 8   | 4    | sample_rate     | 24000                                      |
| 12  | 4    | data_bytes_used | UI usage display                           |
| 16  | 4    | dir_crc32       | CRC32 over the 19 × 32-byte entries        |
| 20  | 12   | reserved        | `0xFF`                                     |

Version history: v1 was the 18-slot pre-pad layout (drums at 2–17). The
web manager migrates v1 directories on read (drum entries shift to 3–18,
sample data stays put) and rewrites them as v2 on Apply; the firmware
rejects anything but v2/19 — reinterpreting an old directory without
migration puts the kick in the pad slot and shifts every drum lane.

### Slot entry (32 bytes each, at 32 + slot × 32)

Slot 0 = LEAD instrument, slot 1 = BASS instrument, slot 2 = PAD instrument,
slots 3–18 = DRUM 1–16.

| Off | Size | Field         | Notes                                              |
|-----|------|---------------|-----------------------------------------------------|
| 0   | 12   | name[12]      | ASCII, NUL padded                                   |
| 12  | 4    | offset        | bytes from bank base; ≥ `0x1000`, 4096-aligned; `0xFFFFFFFF` = empty |
| 16  | 4    | length_frames | int16 mono frames; 0 = empty                        |
| 20  | 4    | sample_rate   | 24000                                               |
| 24  | 1    | root_note     | melodic slots: MIDI note at unity pitch; `0xFF` drums |
| 25  | 1    | assign_note   | drum slots: trigger MIDI note; `0xFF` melodic       |
| 26  | 1    | flags         | bit0 = loop (melodic sustain)                       |
| 27  | 1    | choke_group   | 0 = none, 1–4                                       |
| 28  | 4    | data_crc32    | CRC32 of the PCM bytes                              |

Default drum map (GM-ish, editable in the UI; `assign_note` must be unique):

| Slot | Name       | Note | Choke | Slot | Name       | Note | Choke |
|------|------------|------|-------|------|------------|------|-------|
| 3    | Kick       | 36   | –     | 11   | Mid Tom    | 45   | –     |
| 4    | Rim        | 37   | –     | 12   | Hi Tom     | 48   | –     |
| 5    | Snare      | 38   | –     | 13   | Crash      | 49   | –     |
| 6    | Clap       | 39   | –     | 14   | Ride       | 51   | –     |
| 7    | Closed Hat | 42   | 1     | 15   | Tambourine | 54   | –     |
| 8    | Pedal Hat  | 44   | 1     | 16   | Cowbell    | 56   | –     |
| 9    | Open Hat   | 46   | 1     | 17   | Shaker     | 70   | –     |
| 10   | Low Tom    | 41   | –     | 18   | Clave      | 75   | –     |

## 4. Config blob (`CWCF`, 128 bytes, stored in the config sector)

| Off | Size | Field                    | Notes                                         |
|-----|------|--------------------------|-----------------------------------------------|
| 0   | 4    | magic                    | `"CWCF"` = u32 `0x46435743`                    |
| 4   | 2    | version                  | 1                                              |
| 6   | 1    | engine_mode              | 0 melodic, 1 percussive                        |
| 7   | 1    | active_song              | 0..slots-1                                     |
| 8   | 2    | tempo_bpm_x10            | 400–2400; only seeds the follower's clock-recovery estimate at boot — playback tempo always comes from the song's tempo map |
| 10  | 1    | swing                    | 50–75 (%)                                      |
| 11  | 1    | out_src_bits             | bit0 CV1, bit1 CV2, bit2 Pulse1, bit3 Pulse2: 0 = local, 1 = remote |
| 12  | 1    | fwd_enable               | bit0 CV In1, bit1 CV In2, bit2 Pulse In1, bit3 Pulse In2 |
| 13  | 1    | pulse1_lane              | drum lane (0–15) on PulseOut1; default 0 (kick) |
| 14  | 1    | pulse2_lane              | default 2 (snare)                              |
| 15  | 1    | cv2_lane                 | lane whose velocity drives CVOut2 (percussive); default 0 |
| 16  | 2    | out2_lane_mask           | lanes summed on AudioOut2 (percussive); default `0x0001` |
| 18  | 1    | release_lead             | ms/4 (0–255 → 0–1020 ms); default 15 (60 ms)   |
| 19  | 1    | release_bass             | default 30 (120 ms)                            |
| 20  | 1    | master_vol               | 0–255 boot default; Y knob overrides live      |
| 21  | 1    | release_pad              | ms/4; default 60 (240 ms); 0 (old blobs' reserved byte) reads as the default |
| 22  | 16   | midi_channel_to_part[16] | live USB-MIDI: 0 lead, 1 bass, 2 drums, 3 pad, 0xFF ignore. Default: ch1→0, ch2→1, ch3→3, ch10→2 |
| 38  | 86   | reserved                 | `0xFF`                                         |
| 124 | 4    | crc32                    | over bytes 0..123                              |

Lane n ↔ directory slot 2 + n.

## 5. CDC protocol (`COWORK1`)

USB CDC serial (the card is a composite MIDI + CDC device, VID `0x2E8A`,
PID `0x10C3`, product string `CoWork`). Framing is byte-oriented: commands are
single ASCII bytes followed by fixed-size little-endian arguments, so Web
Serial packet boundaries never matter. Status lines are ASCII terminated by
`\n`.

| Cmd  | Request (after cmd byte)        | Response                                                |
|------|---------------------------------|---------------------------------------------------------|
| `X`  | —                               | `SYNC\n` (safe at any time; resets the parser)          |
| `Q`  | —                               | none; aborts an in-flight `W`/`R`                       |
| `I`  | —                               | u32 len + JSON (schema below)                           |
| `R`  | u32 offset, u32 length          | u32 granted (0 = rejected); then data in 1024-byte chunks, host acks each chunk with `A`; then u32 crc32 + `DONE\n` |
| `W`  | u32 offset, u32 length, u32 crc32 | `OK\n` or `ERR <code>\n`; host streams `length` bytes; firmware erases sectors just-in-time and programs 256-byte pages; then `OK\n` / `CRC\n` / `TIMEOUT\n` |
| `E`  | u32 offset, u32 length          | `OK\n` / `ERR <code>\n` (4 KB multiples)                |
| `G`  | —                               | u32 len (=128) + live config blob                       |
| `S`  | u32 len (=128) + blob + u32 crc32 | `OK\n` / `ERR <code>\n` / `CRC\n`; applies live, flash save debounced ~2 s |
| `T?` | —                               | one JSON status line + `\n`                             |
| `TS` | —                               | `OK\n` — stop transport                                 |
| `TP` | —                               | `OK\n` / `ERR ROLE\n` — start transport (leader only)   |
| `TR` | —                               | `OK\n` — rescan song/sample directories from flash      |

Error codes: `RANGE` (outside songs/samples regions), `ALIGN` (offset not
sector-aligned for W/E), `BUSY` (transport running — send `TS` first), `LEN`,
`ROLE`.

Rules:
- `W`/`E` are rejected with `ERR BUSY` while the transport is running.
  `S` is allowed while playing (it only touches RAM); its flash save is
  deferred until the transport stops.
- `W`/`E`/`R` operate only inside the song-slots region or the sample bank.
- Write ordering for crash safety: data first, directory/header sectors last.
- `W` body has a 2-second inter-byte timeout → `TIMEOUT\n` and abort.
- After any successful `W`/`E` the firmware rescans directories automatically;
  `TR` forces it.

### `I` JSON schema (single line)

```json
{"proto":"COWORK1","fw":"0.1.0","flash":16777216,
 "config":{"off":262144,"len":4096},
 "songs":{"off":266240,"slotLen":65536,"slots":8},
 "samples":{"off":790528,"dataOff":794624,"end":16777216},
 "state":{"playing":false,"role":"leader","linked":false,"mode":0,"song":0,"bpm":120.0}}
```

### `T?` JSON schema (single line)

```json
{"playing":false,"role":"leader","linked":false,"mode":0,"song":0,"bpm":120.0,"tick":0}
```

## 6. MIDI link protocol (module ↔ module, module ↔ DAW)

- Transport/clock: standard MIDI realtime — `0xF8` clock at 24 PPQN, `0xFA`
  start, `0xFB` continue, `0xFC` stop. The leader transmits; the follower
  consumes (from the peer module or any DAW).
- Pulse forwarding uses **MIDI channel 16**; CV forwarding uses **pitch
  bend** on channels 15 (CV 1) and 16 (CV 2) — a single 3-byte message
  carries all 14 bits atomically. (v1 used CC MSB/LSB pairs, which keep
  pairing state across two messages: with both channels streaming, one
  lost message desyncs every later value.)

| Signal          | Message                        | Rate                                  |
|-----------------|--------------------------------|---------------------------------------|
| CV In 1         | Pitch bend, channel 15, 14-bit | on-change ≤ 200 Hz + 250 ms keep-alive |
| CV In 2         | Pitch bend, channel 16, 14-bit | on-change ≤ 200 Hz + 250 ms keep-alive |
| Pulse In 1 edge | Note On/Off 60 ch16, vel 127   | edge-triggered                        |
| Pulse In 2 edge | Note On/Off 62 ch16, vel 127   | edge-triggered                        |

CV encoding: `value14 = clamp(cv + 2048, 0, 4095) << 2` where `cv` is the
±2047 12-bit input reading; decode `cv = (value14 >> 2) - 2048`. 0 V maps
exactly to bend center (8192). Only patched inputs are forwarded; unchanged
values are re-sent every 250 ms as a keep-alive.

Received remote values expire after 500 ms without update (outputs configured
as "remote" fall back to their local derivation) — with the keep-alive that
only happens on genuine link or routing loss.
