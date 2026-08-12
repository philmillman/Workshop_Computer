// In-memory Standard MIDI File writer for test fixtures — no committed
// binaries, everything reviewable.

export function vlq(n) {
  const out = [n & 0x7F];
  n >>= 7;
  while (n > 0) {
    out.unshift((n & 0x7F) | 0x80);
    n >>= 7;
  }
  return out;
}

function u32be(v) { return [v >>> 24 & 0xFF, v >>> 16 & 0xFF, v >>> 8 & 0xFF, v & 0xFF]; }
function u16be(v) { return [v >>> 8 & 0xFF, v & 0xFF]; }

// events: {delta, kind:'on'|'off'|'tempo'|'tsig'|'name'|'raw', ch, note, vel, uspq, num, denomLog2, text, bytes}
export function buildTrack(events, { appendEot = true } = {}) {
  const b = [];
  for (const ev of events) {
    b.push(...vlq(ev.delta || 0));
    switch (ev.kind) {
    case "on": b.push(0x90 | (ev.ch || 0), ev.note, ev.vel === undefined ? 100 : ev.vel); break;
    case "off": b.push(0x80 | (ev.ch || 0), ev.note, 0x40); break;
    case "tempo": b.push(0xFF, 0x51, 0x03, ev.uspq >> 16 & 0xFF, ev.uspq >> 8 & 0xFF, ev.uspq & 0xFF); break;
    case "tsig": b.push(0xFF, 0x58, 0x04, ev.num, ev.denomLog2, 24, 8); break;
    case "name": {
      const t = [...ev.text].map(c => c.charCodeAt(0));
      b.push(0xFF, 0x03, ...vlq(t.length), ...t);
      break;
    }
    case "raw": b.push(...ev.bytes); break;
    }
  }
  if (appendEot) b.push(0x00, 0xFF, 0x2F, 0x00);
  return b;
}

export function buildSMF({ format = 0, division = 96, tracks = [] }) {
  const out = [
    0x4D, 0x54, 0x68, 0x64, // MThd
    ...u32be(6), ...u16be(format), ...u16be(tracks.length), ...u16be(division),
  ];
  for (const trackBytes of tracks) {
    out.push(0x4D, 0x54, 0x72, 0x6B); // MTrk
    out.push(...u32be(trackBytes.length));
    out.push(...trackBytes);
  }
  return new Uint8Array(out);
}

// ---- ready-made fixtures ----

export function simpleFormat0() {
  // ch1: C4 quarter, D4 quarter at 120 BPM, division 96
  return buildSMF({ format: 0, division: 96, tracks: [buildTrack([
    { delta: 0, kind: "tempo", uspq: 500000 },
    { delta: 0, kind: "on", ch: 0, note: 60, vel: 100 },
    { delta: 96, kind: "off", ch: 0, note: 60 },
    { delta: 0, kind: "on", ch: 0, note: 62, vel: 90 },
    { delta: 96, kind: "off", ch: 0, note: 62 },
  ])] });
}

export function format1MultiTrack() {
  // Track 0 = tempo map, track 1 = lead (ch1), track 2 = bass (ch2),
  // track 3 = drums (ch10), division 480
  return buildSMF({ format: 1, division: 480, tracks: [
    buildTrack([
      { delta: 0, kind: "tempo", uspq: 600000 },        // 100 BPM
      { delta: 960, kind: "tempo", uspq: 500000 },      // -> 120 BPM at beat 2
      { delta: 0, kind: "tsig", num: 4, denomLog2: 2 },
    ]),
    buildTrack([
      { delta: 0, kind: "name", text: "Lead" },
      { delta: 0, kind: "on", ch: 0, note: 72, vel: 110 },
      { delta: 480, kind: "off", ch: 0, note: 72 },
    ]),
    buildTrack([
      { delta: 0, kind: "name", text: "Bass" },
      { delta: 0, kind: "on", ch: 1, note: 36, vel: 100 },
      { delta: 960, kind: "off", ch: 1, note: 36 },
    ]),
    buildTrack([
      { delta: 0, kind: "on", ch: 9, note: 36, vel: 127 },
      { delta: 10, kind: "off", ch: 9, note: 36 },
      { delta: 470, kind: "on", ch: 9, note: 38, vel: 127 },
      { delta: 10, kind: "off", ch: 9, note: 38 },
    ]),
  ] });
}

export function runningStatusTrack() {
  // 0x90 3C 64, then running status: 40 64, then zero-velocity offs
  return buildSMF({ format: 0, division: 96, tracks: [buildTrack([
    { delta: 0, kind: "raw", bytes: [0x90, 0x3C, 0x64] },
    { delta: 48, kind: "raw", bytes: [0x40, 0x64] },        // running status note-on E4
    { delta: 48, kind: "raw", bytes: [0x3C, 0x00] },        // zero-vel = note-off C4
    { delta: 0, kind: "raw", bytes: [0x40, 0x00] },         // zero-vel = note-off E4
  ])] });
}

export function overlappingNotes() {
  // Same pitch retriggered before its off
  return buildSMF({ format: 0, division: 96, tracks: [buildTrack([
    { delta: 0, kind: "on", ch: 0, note: 60, vel: 100 },
    { delta: 48, kind: "on", ch: 0, note: 60, vel: 100 },
    { delta: 48, kind: "off", ch: 0, note: 60 },
    { delta: 48, kind: "off", ch: 0, note: 60 },
  ])] });
}

export function danglingNote() {
  return buildSMF({ format: 0, division: 96, tracks: [buildTrack([
    { delta: 0, kind: "on", ch: 0, note: 64, vel: 100 },
    // no off before end-of-track
  ])] });
}

export function format2File() {
  return buildSMF({ format: 2, division: 96, tracks: [buildTrack([
    { delta: 0, kind: "on", ch: 0, note: 60 },
  ])] });
}

export function smpteFile() {
  return buildSMF({ format: 0, division: 0xE250 /* SMPTE 30fps */, tracks: [buildTrack([
    { delta: 0, kind: "on", ch: 0, note: 60 },
  ])] });
}

export function hugeFile(noteCount) {
  const ev = [{ delta: 0, kind: "tempo", uspq: 500000 }];
  for (let i = 0; i < noteCount; i++) {
    ev.push({ delta: i === 0 ? 0 : 6, kind: "on", ch: 0, note: 60 + (i % 12), vel: 100 });
    ev.push({ delta: 3, kind: "off", ch: 0, note: 60 + (i % 12) });
    ev[ev.length - 2].delta = i === 0 ? 0 : 3;
  }
  return buildSMF({ format: 0, division: 96, tracks: [buildTrack(ev)] });
}
