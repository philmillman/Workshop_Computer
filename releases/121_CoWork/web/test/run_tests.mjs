// Node test harness for the CoWorkCore block inside cowork.html.
// Zero dependencies: node --test web/test/run_tests.mjs (or just node it).
//
// The core script is extracted from the HTML by its <script id="cowork-core">
// marker and evaluated in this realm so Uint8Array instances interoperate.

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import * as fx from "./fixtures.mjs";

const html = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "cowork.html"), "utf8");
const m = html.match(/<script id="cowork-core">([\s\S]*?)<\/script>/);
if (!m) throw new Error("cowork-core script block not found");
(0, eval)(m[1]);
const C = globalThis.CoWorkCore;

// ---------------------------------------------------------------- crc32
test("crc32 reference vector", () => {
  assert.equal(C.crc32(new TextEncoder().encode("123456789")), 0xCBF43926);
  assert.equal(C.crc32(new Uint8Array(0)), 0);
});

// ---------------------------------------------------------------- flash map
test("flash map matches FORMATS.md", () => {
  const m16 = C.computeMap(16 * 1024 * 1024);
  assert.equal(m16.config.off, 0x040000);
  assert.equal(m16.songs.off, 0x041000);
  assert.equal(m16.songs.slots, 8);
  assert.equal(m16.samples.off, 0x0C1000);
  assert.equal(m16.samples.dataOff, 0x0C2000);
  const m2 = C.computeMap(2 * 1024 * 1024);
  assert.equal(m2.songs.slots, 4);
  assert.equal(m2.samples.off, 0x081000);
});

// ---------------------------------------------------------------- SMF parse
test("parse simple format 0", () => {
  const p = C.parseSMF(fx.simpleFormat0());
  assert.equal(p.format, 0);
  assert.equal(p.division, 96);
  const notes = p.tracks[0].events.filter(e => e.kind === "on");
  assert.equal(notes.length, 2);
  assert.equal(notes[0].note, 60);
  assert.equal(notes[1].tick, 96);
});

test("reject format 2 and SMPTE", () => {
  assert.throws(() => C.parseSMF(fx.format2File()), /format 2/);
  assert.throws(() => C.parseSMF(fx.smpteFile()), /SMPTE/);
  assert.throws(() => C.parseSMF(new Uint8Array([1, 2, 3])), /MIDI/);
});

test("running status and zero-velocity note-offs", () => {
  const p = C.parseSMF(fx.runningStatusTrack());
  const evs = p.tracks[0].events;
  const ons = evs.filter(e => e.kind === "on");
  const offs = evs.filter(e => e.kind === "off");
  assert.equal(ons.length, 2);
  assert.equal(offs.length, 2);
  assert.equal(ons[1].note, 0x40);
  assert.equal(ons[1].tick, 48);
});

test("channel summary and auto-suggest", () => {
  const p = C.parseSMF(fx.format1MultiTrack());
  const sum = C.smfChannelSummary(p);
  assert.equal(sum.length, 3);
  const map = C.suggestChannelMap(sum);
  assert.equal(map[9], 2);   // ch10 -> drums
  assert.equal(map[1], 1);   // low average pitch -> bass
  assert.equal(map[0], 0);   // remaining -> lead
});

// ---------------------------------------------------------------- convert
test("auto-suggest assigns a pad with three melodic channels", () => {
  const smf = fx.buildSMF({ format: 1, division: 96, tracks: [
    fx.buildTrack([{ delta: 0, kind: "on", ch: 0, note: 72 }, { delta: 48, kind: "off", ch: 0, note: 72 },
                   { delta: 0, kind: "on", ch: 0, note: 74 }, { delta: 48, kind: "off", ch: 0, note: 74 },
                   { delta: 0, kind: "on", ch: 0, note: 76 }, { delta: 48, kind: "off", ch: 0, note: 76 }]),
    fx.buildTrack([{ delta: 0, kind: "on", ch: 1, note: 36 }, { delta: 96, kind: "off", ch: 1, note: 36 }]),
    fx.buildTrack([{ delta: 0, kind: "on", ch: 2, note: 60 }, { delta: 96, kind: "off", ch: 2, note: 60 },
                   { delta: 0, kind: "on", ch: 2, note: 63 }, { delta: 96, kind: "off", ch: 2, note: 63 }]),
  ] });
  const map = C.suggestChannelMap(C.smfChannelSummary(C.parseSMF(smf)));
  assert.equal(map[1], 1); // lowest average pitch -> bass
  assert.equal(map[0], 0); // densest -> lead
  assert.equal(map[2], 3); // remaining -> pad
});

test("convert rescales ticks and keeps tempo events", () => {
  const p = C.parseSMF(fx.format1MultiTrack()); // division 480
  const conv = C.convertSMF(p, { 0: 0, 1: 1, 9: 2 });
  // 480-tick quarter -> 96-tick quarter
  const lead = conv.events.filter(e => e.type === 1 && e.part === 0);
  assert.equal(lead[0].tick, 0);
  const leadOff = conv.events.filter(e => e.type === 0 && e.part === 0);
  assert.equal(leadOff[0].tick, 96);
  const tempos = conv.events.filter(e => e.type === 2);
  assert.equal(tempos.length, 2);
  assert.equal(tempos[1].tick, 192);
  assert.equal(conv.tempoUspq, 600000);
  // Bar-rounded length: last event at tick 192+2 -> one 4/4 bar = 384
  assert.equal(conv.lengthTicks, 384);
});

test("convert repairs overlapping same-pitch notes", () => {
  const p = C.parseSMF(fx.overlappingNotes());
  const conv = C.convertSMF(p, { 0: 0 });
  const evs = conv.events.filter(e => e.type !== 2);
  // on@0, off@48(injected), on@48, off@96, (second off dropped as stray)
  assert.equal(evs.length, 4);
  assert.deepEqual(evs.map(e => e.type), [1, 0, 1, 0]);
  // At tick 48 the off sorts before the on
  assert.equal(evs[1].tick, 48);
  assert.equal(evs[2].tick, 48);
});

test("convert closes dangling notes", () => {
  const p = C.parseSMF(fx.danglingNote());
  const conv = C.convertSMF(p, { 0: 0 });
  const offs = conv.events.filter(e => e.type === 0);
  assert.equal(offs.length, 1);
  assert.ok(offs[0].tick >= 1);
});

test("convert flags oversize files as truncated", () => {
  const p = C.parseSMF(fx.hugeFile(5000)); // 10k events > 8184
  const conv = C.convertSMF(p, { 0: 0 });
  assert.ok(conv.stats.truncated);
  assert.ok(conv.stats.bytes <= C.SONG_SLOT_BYTES);
});

// ---------------------------------------------------------------- CWSQ codec
test("song encode/decode round-trip", () => {
  const events = [
    { tick: 0, type: 2, uspq: 480000 },
    { tick: 0, type: 1, part: 0, note: 60, vel: 100 },
    { tick: 96, type: 0, part: 0, note: 60, vel: 0 },
  ];
  const bytes = C.encodeSong({ name: "roundtrip", events, lengthTicks: 384 });
  assert.equal(bytes.length, 64 + 4 * 8); // + END sentinel
  const dec = C.decodeSong(bytes);
  assert.equal(dec.header.name, "roundtrip");
  assert.equal(dec.header.ppqn, 96);
  assert.equal(dec.header.eventCount, 4);
  assert.equal(dec.events[0].uspq, 480000);
  assert.equal(dec.events[1].note, 60);
  assert.equal(dec.events[3].type, 0x7F);
  // CRC covers the event bytes
  const evBytes = bytes.subarray(64);
  assert.equal(dec.header.eventsCrc, C.crc32(evBytes));
});

// ---------------------------------------------------------------- CWCF codec
test("config encode/decode round-trip", () => {
  const c = C.defaultConfig();
  c.engineMode = 1;
  c.tempoBpmX10 = 1337;
  c.outSrcBits = 0b1010;
  c.midiChannelToPart[4] = 2;
  const blob = C.encodeConfig(c);
  assert.equal(blob.length, 128);
  const d = C.decodeConfig(blob);
  assert.deepEqual(d, { ...c });
  // CRC field is valid
  assert.equal(C.readU32(blob, 124), C.crc32(blob.subarray(0, 124)));
});

// ---------------------------------------------------------------- CWSB codec
test("bank directory round-trip and allocator", () => {
  const slots = new Array(19).fill(null).map(() => ({}));
  slots[0] = { name: "lead", offset: 0x1000, lengthFrames: 24000, root: 60, assignNote: 0xFF, flags: 1, choke: 0, dataCrc: 123 };
  slots[2] = { name: "pad", offset: 0xFFFFFFFF, lengthFrames: 8000, root: 62, assignNote: 0xFF, flags: 1, choke: 0, dataCrc: 44, dirty: true };
  slots[3] = { name: "kick", offset: 0xFFFFFFFF, lengthFrames: 12000, root: 0xFF, assignNote: 36, choke: 0, dataCrc: 45, dirty: true };
  slots[4] = { name: "hat", offset: 0xFFFFFFFF, lengthFrames: 6000, root: 0xFF, assignNote: 42, choke: 1, dataCrc: 46, dirty: true };

  const plan = C.allocateBank(slots, 1024 * 1024);
  assert.ok(plan);
  // lead is fixed at 0x1000 (48000 bytes -> 12 sectors -> ends 0xD000);
  // dirty slots pack after it, 4K aligned, without overlapping
  const ranges = [slots[2], slots[3], slots[4]].map(sl =>
    [sl.offset, sl.offset + Math.ceil(sl.lengthFrames * 2 / 4096) * 4096]);
  for (const [a] of ranges) {
    assert.ok(a >= 0xD000);
    assert.equal(a % 4096, 0);
  }
  ranges.sort((x, y) => x[0] - y[0]);
  for (let i = 1; i < ranges.length; i++)
    assert.ok(ranges[i][0] >= ranges[i - 1][1]);

  const dir = C.encodeBankDir(slots, plan.used);
  assert.equal(dir.length, 4096);
  const dec = C.decodeBankDir(dir);
  assert.equal(dec.slots[0].name, "lead");
  assert.equal(dec.slots[0].flags & 1, 1);
  assert.equal(dec.slots[2].root, 62);
  assert.equal(dec.slots[3].assignNote, 36);
  assert.equal(dec.slots[4].choke, 1);

  // Doesn't fit -> null
  const big = new Array(19).fill(null).map(() => ({}));
  big[2] = { lengthFrames: 10 * 1024 * 1024, offset: 0xFFFFFFFF, dirty: true };
  assert.equal(C.allocateBank(big, 1024 * 1024), null);
});

// ---------------------------------------------------------------- audio utils
test("audio utility functions", () => {
  const f = new Float32Array([0, 0.5, -0.5, 0]);
  const n = C.normalizePeak(f, 0);
  assert.ok(Math.abs(n[1] - 1.0) < 1e-6);
  const i16 = C.floatToInt16(new Float32Array([0, 1, -1, 2]));
  assert.deepEqual([...i16], [0, 32767, -32767, 32767]);
  const trimmed = C.trimSilence(new Float32Array([0, 0, 0, 0.5, 0.5, 0, 0]), 0.01);
  assert.ok(trimmed.length < 7 && trimmed.includes(0.5));
  const mono = C.mixToMono([new Float32Array([1, 0]), new Float32Array([0, 1])]);
  assert.deepEqual([...mono], [0.5, 0.5]);
});

// ---------------------------------------------------------------- protocol vs mock
function makeLink(opts) {
  const mock = new C.MockDevice(opts?.flash || 16 * 1024 * 1024, { fragment: true, seed: opts?.seed || 7 });
  return { mock, proto: new C.Protocol(mock) };
}

test("protocol: sync + info against fragmented mock", async () => {
  const { proto } = makeLink({ seed: 99 });
  await proto.sync();
  const info = await proto.info();
  assert.equal(info.proto, "COWORK1");
  assert.equal(info.songs.slots, 8);
  assert.equal(info.samples.off, 0x0C1000);
});

test("protocol: write/read round-trip with CRC", async () => {
  const { proto, mock } = makeLink({ seed: 3 });
  await proto.sync();
  const data = new Uint8Array(10000);
  for (let i = 0; i < data.length; i++) data[i] = (i * 7 + 13) & 0xFF;
  const off = mock.map.samples.dataOff;
  await proto.write(off, data);
  const back = await proto.read(off, data.length);
  assert.deepEqual(back, data);
});

test("protocol: unaligned/out-of-range writes rejected", async () => {
  const { proto, mock } = makeLink();
  await proto.sync();
  await assert.rejects(proto.write(mock.map.samples.dataOff + 1, new Uint8Array(4096)), /ALIGN/);
  await assert.rejects(proto.write(0, new Uint8Array(4096)), /RANGE/);
  await assert.rejects(proto.read(0, 64), /rejected/);
});

test("protocol: BUSY while playing, transport control", async () => {
  const { proto, mock } = makeLink();
  await proto.sync();
  await proto.transport("P");
  let st = await proto.transport("?");
  assert.equal(st.playing, true);
  await assert.rejects(proto.write(mock.map.samples.dataOff, new Uint8Array(4096)), /BUSY/);
  await proto.transport("S");
  st = await proto.transport("?");
  assert.equal(st.playing, false);
  // Follower can't start from the web
  mock.state.role = "follower";
  await assert.rejects(proto.transport("P"), /ROLE/);
});

test("protocol: config get/set round-trip", async () => {
  const { proto } = makeLink({ seed: 42 });
  await proto.sync();
  const c = await proto.getConfig();
  c.engineMode = 1;
  c.pulse2Lane = 5;
  await proto.setConfig(c);
  const c2 = await proto.getConfig();
  assert.equal(c2.engineMode, 1);
  assert.equal(c2.pulse2Lane, 5);
});

test("protocol: corrupted write reports CRC", async () => {
  const { proto, mock } = makeLink();
  await proto.sync();
  mock.corruptNextWrite = true;
  await assert.rejects(proto.write(mock.map.samples.dataOff, new Uint8Array(4096).fill(9)), /CRC/);
});

test("protocol: erase then read back 0xFF", async () => {
  const { proto, mock } = makeLink();
  await proto.sync();
  const off = mock.map.songs.off;
  await proto.write(off, C.encodeSong({ name: "x", events: [], lengthTicks: 384 }));
  await proto.erase(off, 4096);
  const back = await proto.read(off, 64);
  assert.ok(back.every(b => b === 0xFF));
});

test("protocol: concurrent commands serialize (poller during upload)", async () => {
  // Regression: the status poller used to fire mid-upload, injecting its
  // 'T?' bytes into the W body (firmware saw them as sample data -> CRC)
  // and stealing response lines (transport() reported a JSON status line
  // as its error). The Protocol mutex must make this safe.
  const { proto, mock } = makeLink({ seed: 5 });
  await proto.sync();
  const data = new Uint8Array(60000);
  for (let i = 0; i < data.length; i++) data[i] = (i * 31 + 7) & 0xFF;
  const off = mock.map.samples.dataOff;
  const results = await Promise.all([
    proto.write(off, data),
    proto.transport("?"),   // simulated poll
    proto.getConfig(),      // simulated live config edit
    proto.transport("?"),   // another poll
  ]);
  assert.equal(results[1].playing, false);
  assert.equal(typeof results[2].engineMode, "number");
  const back = await proto.read(off, data.length);
  assert.deepEqual(back, data);
});

test("end-to-end: stage a song via the codecs and mock card", async () => {
  const { proto, mock } = makeLink({ seed: 11 });
  await proto.sync();
  const parsed = C.parseSMF(fx.format1MultiTrack());
  const conv = C.convertSMF(parsed, { 0: 0, 1: 1, 9: 2 });
  const song = C.encodeSong({ name: "e2e", events: conv.events,
                              lengthTicks: conv.lengthTicks, tempoUspq: conv.tempoUspq, tsig: conv.tsig });
  const off = mock.map.songs.off + 2 * mock.map.songs.slotLen;
  await proto.write(off, song);
  const hdr = C.decodeSongHeader(await proto.read(off, 64));
  assert.equal(hdr.name, "e2e");
  assert.equal(hdr.tempoUspq, 600000);
  const full = C.decodeSong(await proto.read(off, song.length));
  assert.equal(full.events.length, hdr.eventCount);
});
