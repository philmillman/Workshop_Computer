// Generates the example content in this folder:
//   midi/*.mid      — test songs (lead ch1, bass ch2, pad ch3, drums ch10)
//   samples/*.wav   — a synthesized drum kit + lead/bass instruments
//                     (16-bit mono 24 kHz, the card's native format)
//
// Everything is synthesized from code so the examples are reviewable and
// license-clean. Re-run with:  node examples/generate.mjs

import { writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = dirname(fileURLToPath(import.meta.url));
mkdirSync(join(ROOT, "midi"), { recursive: true });
mkdirSync(join(ROOT, "samples"), { recursive: true });

// ============================================================ WAV writing

const RATE = 24000;

function writeWav(name, f32) {
  // normalize to -1 dBFS
  let peak = 1e-9;
  for (const s of f32) peak = Math.max(peak, Math.abs(s));
  const g = Math.pow(10, -1 / 20) / peak;

  const n = f32.length;
  const buf = Buffer.alloc(44 + n * 2);
  buf.write("RIFF", 0);
  buf.writeUInt32LE(36 + n * 2, 4);
  buf.write("WAVE", 8);
  buf.write("fmt ", 12);
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20);        // PCM
  buf.writeUInt16LE(1, 22);        // mono
  buf.writeUInt32LE(RATE, 24);
  buf.writeUInt32LE(RATE * 2, 28);
  buf.writeUInt16LE(2, 32);
  buf.writeUInt16LE(16, 34);
  buf.write("data", 36);
  buf.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) {
    let v = Math.round(f32[i] * g * 32767);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    buf.writeInt16LE(v, 44 + i * 2);
  }
  writeFileSync(join(ROOT, "samples", name), buf);
  console.log(`samples/${name}  ${(n / RATE).toFixed(2)}s  ${(buf.length / 1024).toFixed(0)}KB`);
}

// Deterministic noise so re-runs produce identical files
let rngState = 0xC0FFEE;
function rand() {
  rngState = (rngState * 1664525 + 1013904223) >>> 0;
  return rngState / 0x100000000 * 2 - 1;
}

function render(seconds, fn) {
  const n = Math.round(seconds * RATE);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = fn(i / RATE, i, out);
  return out;
}

const TAU = Math.PI * 2;

// One-pole highpassed noise: bright, hat-like
function makeHpNoise(strength = 1) {
  let last = 0;
  return () => {
    const w = rand();
    const v = w - last * strength;
    last = w;
    return v * 0.7;
  };
}

// ============================================================ drum kit

function drums() {
  // Kick: pitch sweep 95->42 Hz, exp decay, soft click
  {
    let phase = 0;
    writeWav("kick.wav", render(0.35, t => {
      const f = 42 + 53 * Math.exp(-t * 28);
      phase += TAU * f / RATE;
      const click = t < 0.004 ? rand() * 0.4 * (1 - t / 0.004) : 0;
      return Math.sin(phase) * Math.exp(-t * 16) + click;
    }));
  }

  // Snare: tone burst + noise
  {
    const hp = makeHpNoise(0.4);
    writeWav("snare.wav", render(0.26, t =>
      Math.sin(TAU * 186 * t) * 0.5 * Math.exp(-t * 42) +
      hp() * 0.8 * Math.exp(-t * 20)));
  }

  // Rim: short bright ping
  writeWav("rim.wav", render(0.06, t =>
    Math.sin(TAU * 1180 * t) * Math.exp(-t * 110) + (t < 0.002 ? rand() * 0.5 : 0)));

  // Clap: three retriggered bursts + tail
  {
    const hp = makeHpNoise(0.55);
    writeWav("clap.wav", render(0.3, t => {
      const bursts = [0, 0.011, 0.022];
      let env = 0;
      for (const b of bursts)
        if (t >= b) env = Math.max(env, Math.exp(-(t - b) * 90));
      env = Math.max(env, 0.5 * Math.exp(-t * 14));
      return hp() * env;
    }));
  }

  // Hats: highpassed noise, short/pedal/open share a voice
  {
    const hp = makeHpNoise(0.95);
    writeWav("hat_closed.wav", render(0.09, t => hp() * Math.exp(-t * 70)));
  }
  {
    const hp = makeHpNoise(0.95);
    writeWav("hat_pedal.wav", render(0.14, t => hp() * Math.exp(-t * 45)));
  }
  {
    const hp = makeHpNoise(0.95);
    writeWav("hat_open.wav", render(0.55, t => hp() * Math.exp(-t * 7.5)));
  }

  // Toms: kick-like sweeps at three pitches
  for (const [name, base] of [["tom_low.wav", 82], ["tom_mid.wav", 110], ["tom_hi.wav", 150]]) {
    let phase = 0;
    writeWav(name, render(0.3, t => {
      const f = base * (1 + 0.5 * Math.exp(-t * 30));
      phase += TAU * f / RATE;
      return Math.sin(phase) * Math.exp(-t * 14);
    }));
  }

  // Crash: noise + inharmonic shimmer
  {
    const hp = makeHpNoise(0.9);
    writeWav("crash.wav", render(1.3, t =>
      hp() * 0.8 * Math.exp(-t * 3.2) +
      (Math.sin(TAU * 3211 * t) + Math.sin(TAU * 4173 * t) + Math.sin(TAU * 5317 * t)) *
        0.06 * Math.exp(-t * 4)));
  }

  // Ride: quieter, pingier
  {
    const hp = makeHpNoise(0.9);
    writeWav("ride.wav", render(0.9, t =>
      hp() * 0.25 * Math.exp(-t * 4.5) +
      (Math.sin(TAU * 2933 * t) + Math.sin(TAU * 4787 * t) * 0.6) *
        0.18 * Math.exp(-t * 5)));
  }

  // Tambourine: jingles
  {
    const hp = makeHpNoise(0.92);
    writeWav("tambourine.wav", render(0.28, t =>
      hp() * 0.5 * Math.exp(-t * 18) +
      (Math.sin(TAU * 5120 * t + Math.sin(TAU * 63 * t) * 3) +
       Math.sin(TAU * 6431 * t)) * 0.2 * Math.exp(-t * 15)));
  }

  // Cowbell: two detuned squares
  writeWav("cowbell.wav", render(0.22, t => {
    const sq = f => Math.sign(Math.sin(TAU * f * t));
    return (sq(556) * 0.6 + sq(845) * 0.5) * Math.exp(-t * 22);
  }));

  // Shaker: bandpassy noise with an attack ramp
  {
    const hp = makeHpNoise(0.85);
    writeWav("shaker.wav", render(0.13, t => {
      const attack = Math.min(1, t / 0.02);
      return hp() * attack * Math.exp(-t * 30);
    }));
  }

  // Clave: pure decaying ping
  writeWav("clave.wav", render(0.09, t =>
    Math.sin(TAU * 2470 * t) * Math.exp(-t * 55)));
}

// ============================================================ instruments

function instruments() {
  // Lead: two detuned saws (additive, band-limited-ish), plucky attack into
  // a steady sustain so the card's second-half loop works. Root C4 (60).
  {
    const f0 = 261.63;
    writeWav("lead_c4.wav", render(1.0, t => {
      let s = 0;
      for (let h = 1; h <= 10; h++) {
        s += Math.sin(TAU * f0 * h * t * 1.0015) / h;
        s += Math.sin(TAU * f0 * h * t * 0.9985) / h;
      }
      const env = t < 0.006 ? t / 0.006
                : 0.55 + 0.45 * Math.exp(-(t - 0.006) * 7);
      const fade = t > 0.96 ? (1.0 - t) / 0.04 : 1;
      return s * 0.1 * env * fade;
    }));
  }

  // Bass: sine + a little 2nd harmonic and drive, steady sustain. Root C2 (36).
  {
    const f0 = 65.41;
    writeWav("bass_c2.wav", render(1.0, t => {
      const raw = Math.sin(TAU * f0 * t) + 0.35 * Math.sin(TAU * f0 * 2 * t + 0.5);
      const env = t < 0.008 ? t / 0.008 : 1;
      const fade = t > 0.96 ? (1.0 - t) / 0.04 : 1;
      return Math.tanh(raw * 1.6) * env * fade;
    }));
  }

  // Pad: soft detuned triangle stack, slow attack, steady sustain for the
  // 3-voice chord part. Root C4 (60).
  {
    const f0 = 261.63;
    const tri = x => 2 * Math.abs(2 * (x - Math.floor(x + 0.5))) - 1;
    writeWav("pad_c4.wav", render(1.2, t => {
      let ssum = 0;
      for (const det of [0.996, 1.0, 1.005])
        ssum += tri(f0 * det * t) + 0.3 * tri(f0 * det * 2 * t);
      const env = Math.min(1, t / 0.08);
      const fade = t > 1.14 ? (1.2 - t) / 0.06 : 1;
      return ssum * 0.22 * env * fade;
    }));
  }
}

// ============================================================ MIDI writing

function vlq(n) {
  const out = [n & 0x7F];
  n >>= 7;
  while (n > 0) { out.unshift((n & 0x7F) | 0x80); n >>= 7; }
  return out;
}

const PPQ = 480;

// Track builder collecting absolute-time events, emitted sorted with deltas
class Track {
  constructor() { this.evs = []; }
  note(tick, ch, note, vel, lenTicks) {
    this.evs.push({ tick, bytes: [0x90 | ch, note, vel] });
    this.evs.push({ tick: tick + lenTicks, bytes: [0x80 | ch, note, 64] });
  }
  tempo(tick, bpm) {
    const uspq = Math.round(60000000 / bpm);
    this.evs.push({ tick, bytes: [0xFF, 0x51, 0x03, uspq >> 16 & 0xFF, uspq >> 8 & 0xFF, uspq & 0xFF] });
  }
  tsig(tick, num, denomLog2) {
    this.evs.push({ tick, bytes: [0xFF, 0x58, 0x04, num, denomLog2, 24, 8] });
  }
  name(text) {
    this.evs.push({ tick: 0, bytes: [0xFF, 0x03, text.length, ...[...text].map(c => c.charCodeAt(0))] });
  }
  bytes() {
    this.evs.sort((a, b) => a.tick - b.tick);
    const out = [];
    let last = 0;
    for (const ev of this.evs) {
      out.push(...vlq(ev.tick - last), ...ev.bytes);
      last = ev.tick;
    }
    out.push(0x00, 0xFF, 0x2F, 0x00);
    return out;
  }
}

function writeSMF(name, tracks) {
  const u32 = v => [v >>> 24 & 0xFF, v >>> 16 & 0xFF, v >>> 8 & 0xFF, v & 0xFF];
  const u16 = v => [v >>> 8 & 0xFF, v & 0xFF];
  const out = [0x4D, 0x54, 0x68, 0x64, ...u32(6), ...u16(1), ...u16(tracks.length), ...u16(PPQ)];
  for (const t of tracks) {
    const b = t.bytes();
    out.push(0x4D, 0x54, 0x72, 0x6B, ...u32(b.length), ...b);
  }
  writeFileSync(join(ROOT, "midi", name), Buffer.from(out));
  console.log(`midi/${name}  ${(out.length / 1024).toFixed(1)}KB`);
}

// Note helpers. Grid times in beats (quarters), lengths in beats.
const B = PPQ;                       // ticks per beat
const S = PPQ / 4;                   // ticks per 16th
const K = 36, SN = 38, CH = 42, OH = 46, CLP = 39, RD = 51, CB = 56, SH = 70, TL = 41, TM = 45, TH = 48, CLV = 75;

// ---- Song 1: dead-simple smoke test, 2 bars, 120 BPM
function songFirstTest() {
  const meta = new Track();
  meta.name("first test");
  meta.tempo(0, 120);
  meta.tsig(0, 4, 2);

  const lead = new Track();
  lead.name("Lead");
  const arp = [60, 64, 67, 71, 72, 67, 64, 62];
  for (let bar = 0; bar < 2; bar++)
    arp.forEach((n, i) => lead.note((bar * 8 + i) * (B / 2), 0, n, 96, B / 2 - 20));

  const bass = new Track();
  bass.name("Bass");
  const roots = [36, 36, 43, 41];
  for (let bar = 0; bar < 2; bar++)
    roots.forEach((n, i) => bass.note(bar * 4 * B + i * B, 1, n, 100, B - 40));

  const pad = new Track();
  pad.name("Pad");
  const chords1 = [[60, 64, 67], [57, 60, 64]]; // C, Am
  chords1.forEach((chord, bar) => {
    for (const n of chord) pad.note(bar * 4 * B, 2, n, 72, 4 * B - 40);
  });

  const drums = new Track();
  drums.name("Drums");
  for (let bar = 0; bar < 2; bar++) {
    const base = bar * 4 * B;
    for (let q = 0; q < 4; q++) drums.note(base + q * B, 9, K, 120, S);
    drums.note(base + 1 * B, 9, SN, 110, S);
    drums.note(base + 3 * B, 9, SN, 110, S);
    for (let e = 0; e < 8; e++) drums.note(base + e * (B / 2), 9, CH, e % 2 ? 70 : 90, S / 2);
  }
  writeSMF("01_first_test.mid", [meta, lead, bass, pad, drums]);
}

// ---- Song 2: 4-bar groove, 100 BPM, velocities + choke test (open/closed hats)
function songGroove() {
  const meta = new Track();
  meta.name("groove");
  meta.tempo(0, 100);
  meta.tsig(0, 4, 2);

  const lead = new Track();
  lead.name("Lead");
  // Sparse dorian melody with held notes across beats
  const mel = [
    [0.0, 63, 1.5, 92], [2.0, 65, 0.5, 78], [2.5, 67, 1.0, 84], [3.75, 70, 0.25, 60],
    [4.0, 68, 2.0, 95], [6.5, 65, 0.5, 72], [7.0, 63, 1.0, 88],
    [8.0, 60, 1.5, 90], [10.0, 62, 0.5, 70], [10.5, 63, 1.5, 86],
    [12.0, 65, 0.75, 92], [13.0, 67, 0.75, 80], [14.0, 70, 1.75, 98],
  ];
  for (const [beat, n, len, vel] of mel)
    lead.note(Math.round(beat * B), 0, n, vel, Math.round(len * B) - 15);

  const bass = new Track();
  bass.name("Bass");
  // Syncopated 16th riff: C dorian
  const riff = [
    [0.0, 36, 0.75], [1.0, 36, 0.25], [1.5, 39, 0.5], [2.5, 41, 0.5], [3.5, 34, 0.5],
  ];
  for (let bar = 0; bar < 4; bar++)
    for (const [beat, n, len] of riff) {
      const v = 85 + ((bar + beat) * 13 % 30) | 0;
      bass.note(Math.round((bar * 4 + beat) * B), 1, bar === 3 && beat === 3.5 ? n + 12 : n, v, Math.round(len * B) - 20);
    }

  const pad = new Track();
  pad.name("Pad");
  const chords2 = [[60, 63, 67], [56, 60, 63], [58, 62, 65], [60, 63, 67]]; // Cm, Ab, Bb, Cm
  chords2.forEach((chord, bar) => {
    for (const n of chord) pad.note(bar * 4 * B, 2, n, 64, 4 * B - 60);
  });

  const drums = new Track();
  drums.name("Drums");
  for (let bar = 0; bar < 4; bar++) {
    const base = bar * 4 * B;
    // Kick: 1, 2.5+, occasional pickup
    drums.note(base, 9, K, 122, S);
    drums.note(base + 1.75 * B, 9, K, 100, S);
    drums.note(base + 2.5 * B, 9, K, 115, S);
    if (bar % 2 === 1) drums.note(base + 3.75 * B, 9, K, 85, S);
    // Snare 2 & 4 + ghost
    drums.note(base + 1 * B, 9, SN, 112, S);
    drums.note(base + 3 * B, 9, SN, 118, S);
    drums.note(base + 3.25 * B, 9, SN, 45, S / 2);
    // Clap layered on 4
    drums.note(base + 3 * B, 9, CLP, 90, S);
    // Hats: 16ths, open on the last offbeat (chokes against closed)
    for (let s16 = 0; s16 < 16; s16++) {
      const t = base + s16 * S;
      if (s16 === 14) drums.note(t, 9, OH, 95, S * 2);
      else drums.note(t, 9, CH, s16 % 4 === 0 ? 92 : 58 + (s16 * 7) % 20, S / 2);
    }
    // Shaker 8th offbeats
    for (let e = 0; e < 4; e++) drums.note(base + (e + 0.5) * B, 9, SH, 66, S / 2);
    // Fill into the loop on bar 4 (kept inside the bar so the loop stays 4 bars)
    if (bar === 3) {
      drums.note(base + 3.5 * B, 9, TH, 96, S / 2);
      drums.note(base + 3.625 * B, 9, TM, 100, S / 2);
      drums.note(base + 3.75 * B, 9, TL, 108, S / 2);
      drums.note(base + 3.875 * B, 9, CB, 80, S / 2);
    }
  }
  writeSMF("02_groove.mid", [meta, lead, bass, pad, drums]);
}

// ---- Song 3: tempo-map test, 8 bars accelerating 90 -> 132 BPM
function songTempoRide() {
  const meta = new Track();
  meta.name("tempo ride");
  meta.tsig(0, 4, 2);
  for (let bar = 0; bar < 8; bar++)
    meta.tempo(bar * 4 * B, 90 + bar * 6);

  const lead = new Track();
  lead.name("Lead");
  const scale = [60, 62, 63, 65, 67, 68, 70, 72];
  for (let bar = 0; bar < 8; bar++) {
    lead.note(bar * 4 * B, 0, scale[bar], 90, B * 2 - 20);
    lead.note(bar * 4 * B + 2 * B, 0, scale[bar] + 7 <= 79 ? scale[bar] + 7 : scale[bar] - 5, 80, B - 20);
    lead.note(bar * 4 * B + 3 * B, 0, scale[bar], 70, B - 40);
  }

  const bass = new Track();
  bass.name("Bass");
  for (let bar = 0; bar < 8; bar++)
    for (let q = 0; q < 4; q++)
      bass.note((bar * 4 + q) * B, 1, q % 2 ? 48 : 36, 95, B / 2);

  const pad = new Track();
  pad.name("Pad");
  for (let half = 0; half < 4; half++) {
    const chord = half % 2 ? [56, 60, 63] : [60, 63, 67]; // Ab / Cm
    for (const n of chord) pad.note(half * 8 * B, 2, n, 68, 8 * B - 60);
  }

  const drums = new Track();
  drums.name("Drums");
  for (let bar = 0; bar < 8; bar++) {
    const base = bar * 4 * B;
    for (let q = 0; q < 4; q++) {
      drums.note(base + q * B, 9, K, 118, S);
      drums.note(base + (q + 0.5) * B, 9, OH, 78, S);
    }
    drums.note(base + 1 * B, 9, CLV, 100, S / 2);
    drums.note(base + 3 * B, 9, SN, 110, S);
    if (bar === 7) drums.note(base + 3.5 * B, 9, RD, 100, B / 2);
  }
  writeSMF("03_tempo_ride.mid", [meta, lead, bass, pad, drums]);
}


// ---- Song 4: song-length arrangement for long-run testing.
// 64 bars (~2:17 at 112 BPM), Am-F-C-G, eight 8-bar sections:
// intro, verse, verse, chorus, break, build, chorus, outro — with a
// gentle ritardando over the final two bars.
function songLongHaul() {
  const meta = new Track();
  meta.name("long haul");
  meta.tempo(0, 112);
  meta.tsig(0, 4, 2);
  meta.tempo(62 * 4 * B, 105);
  meta.tempo(63 * 4 * B, 96);

  const lead = new Track(); lead.name("Lead");
  const bass = new Track(); bass.name("Bass");
  const pad = new Track(); pad.name("Pad");
  const dr = new Track(); dr.name("Drums");

  const CRASH = 49, RIM = 37;
  const CHORDS = [
    { r: 33, tri: [57, 60, 64] }, // Am
    { r: 29, tri: [53, 57, 60] }, // F
    { r: 36, tri: [60, 64, 67] }, // C
    { r: 31, tri: [55, 59, 62] }, // G
  ];
  const SEC = ["intro", "verse", "verse", "chorus", "break", "build", "chorus", "outro"];

  for (let bar = 0; bar < 64; bar++) {
    const t0 = bar * 4 * B;
    const sec = SEC[(bar / 8) | 0];
    const ch = CHORDS[((bar % 8) / 2) | 0]; // two bars per chord
    const secBar = bar % 8;

    // ---- PAD: sustained 2-bar triads; thin in intro/break, gone in build
    if (bar % 2 === 0 && sec !== "build") {
      const play = sec === "break" ? bar % 4 === 0 : sec === "intro" ? bar >= 4 : true;
      if (play)
        for (const n of ch.tri)
          pad.note(t0, 2, n, sec === "chorus" ? 80 : 62, 8 * B - 60);
    }

    // ---- BASS
    if (sec === "intro" || sec === "break") {
      if (bar % 2 === 0) bass.note(t0, 1, ch.r, 82, 4 * B - 40);
    } else if (sec === "outro") {
      bass.note(t0, 1, ch.r, 84, 3 * B);
    } else if (sec === "verse") {
      for (let e = 0; e < 8; e++)
        bass.note(t0 + e * (B / 2), 1, e === 6 ? ch.r + 12 : ch.r,
                  e % 2 ? 74 : 92, B / 2 - 20);
    } else if (sec === "chorus") {
      const riff = [[0, 0], [0.75, 0], [1.5, 0], [2, 7], [2.5, 0], [3, 10], [3.5, 12]];
      for (const [beat, iv] of riff)
        bass.note(t0 + beat * B, 1, ch.r + iv, iv ? 96 : 88, S + S / 2);
    } else if (sec === "build") {
      const div = secBar < 4 ? 2 : 4; // 8ths, then 16ths
      for (let e = 0; e < 4 * div; e++)
        bass.note(t0 + e * (B / div), 1, ch.r, 68 + e * 2, B / div - 15);
    }

    // ---- LEAD
    if (sec === "verse") {
      if (bar % 2 === 1) {
        const m = [[0, ch.tri[2] + 12, 86, 1], [1.5, ch.tri[1] + 12, 74, 0.5],
                   [2.5, ch.tri[0] + 12, 80, 1.25]];
        for (const [beat, n, v, len] of m)
          lead.note(t0 + beat * B, 0, n, v, Math.round(len * B) - 20);
      }
    } else if (sec === "chorus") {
      for (let e = 0; e < 8; e++) {
        const n = ch.tri[e % 3] + (e % 4 === 3 ? 24 : 12);
        lead.note(t0 + e * (B / 2), 0, n, e % 2 ? 78 : 96, B / 2 - 25);
      }
    } else if (sec === "break") {
      if (bar % 2 === 0) lead.note(t0 + B, 0, ch.tri[0] + 12, 58, 2 * B);
    } else if (sec === "build") {
      const div = secBar < 4 ? 2 : 4;
      for (let e = 0; e < 4 * div; e++)
        lead.note(t0 + e * (B / div), 0, ch.tri[e % 3] + 12, 64 + e * 2, B / div - 15);
    } else if (sec === "outro") {
      if (bar % 2 === 0 && bar < 62)
        lead.note(t0, 0, ch.tri[2] + 12, 72, 2 * B);
      if (bar === 62) lead.note(t0, 0, 69, 84, 6 * B); // final A4 through the ritard
    }

    // ---- DRUMS
    const crash = (sec === "chorus" || sec === "outro") && secBar === 0;
    if (crash) dr.note(t0, 9, CRASH, 110, 2 * B);
    if (sec === "intro") {
      if (bar >= 4) {
        for (let e = 0; e < 8; e++) dr.note(t0 + e * (B / 2), 9, CH, e % 2 ? 52 : 68, S / 2);
        if (bar >= 6) { dr.note(t0 + 1 * B, 9, RIM, 64, S); dr.note(t0 + 3 * B, 9, RIM, 64, S); }
      }
    } else if (sec === "verse") {
      dr.note(t0, 9, K, 118, S);
      dr.note(t0 + 2.5 * B, 9, K, 104, S);
      dr.note(t0 + 1 * B, 9, SN, 108, S);
      dr.note(t0 + 3 * B, 9, SN, 112, S);
      if (bar % 2 === 1) dr.note(t0 + 3.75 * B, 9, SN, 44, S / 2);
      for (let e = 0; e < 8; e++) dr.note(t0 + e * (B / 2), 9, CH, e % 2 ? 58 : 84, S / 2);
    } else if (sec === "chorus") {
      for (let q = 0; q < 4; q++) dr.note(t0 + q * B, 9, K, 118, S);
      dr.note(t0 + 1 * B, 9, SN, 114, S);
      dr.note(t0 + 3 * B, 9, SN, 118, S);
      dr.note(t0 + 3 * B, 9, CLP, 92, S);
      for (let e = 0; e < 8; e++) {
        if (e === 7) dr.note(t0 + e * (B / 2), 9, OH, 92, S * 2);
        else dr.note(t0 + e * (B / 2), 9, CH, e % 2 ? 60 : 86, S / 2);
      }
      dr.note(t0 + 2 * B, 9, SH, 62, S / 2);
    } else if (sec === "break") {
      dr.note(t0, 9, K, 108, S);
      for (let e = 0; e < 4; e++) dr.note(t0 + (e + 0.5) * B, 9, SH, 56 + e * 4, S / 2);
    } else if (sec === "build") {
      for (let q = 0; q < 4; q++) dr.note(t0 + q * B, 9, K, 118, S);
      if (secBar < 4) dr.note(t0 + 3 * B, 9, SN, 100 + secBar * 4, S);
      else if (secBar < 7)
        for (let e = 0; e < 8; e++) dr.note(t0 + e * (B / 2), 9, SN, 80 + e * 3, S / 2);
      else {
        for (let e = 0; e < 12; e++) dr.note(t0 + e * S, 9, SN, 70 + e * 4, S / 2);
        dr.note(t0 + 3 * B, 9, TH, 104, S / 2);
        dr.note(t0 + 3.25 * B, 9, TM, 110, S / 2);
        dr.note(t0 + 3.5 * B, 9, TL, 116, S / 2);
        dr.note(t0 + 3.75 * B, 9, CRASH, 96, S / 2);
      }
    } else if (sec === "outro") {
      dr.note(t0, 9, K, 110 - secBar * 6, S);
      if (secBar < 6) dr.note(t0 + 3 * B, 9, SN, 96 - secBar * 8, S);
      for (let q = 0; q < 4; q++) dr.note(t0 + q * B, 9, RD, 66 - secBar * 4, S);
    }
  }
  writeSMF("04_long_haul.mid", [meta, lead, bass, pad, dr]);
}

// ============================================================ run

drums();
instruments();
songFirstTest();
songGroove();
songTempoRide();
songLongHaul();
console.log("done");
