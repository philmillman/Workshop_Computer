// Engine: envelopes, pitch, allocation/stealing, choke groups, mix paths.

#include "test_common.h"
#include "../src/engine/Engine.h"

#include <cmath>
#include <vector>

using namespace cowork;

// A constant-value "sample" makes level math easy to verify
static std::vector<int16_t> MakeConst(uint32_t frames, int16_t value)
{
	return std::vector<int16_t>(frames, value);
}

static void TestPitchTable()
{
	CHECK_EQ(PitchIncQ12(60, 60), kUnityIncQ12);
	CHECK_EQ(PitchIncQ12(72, 60), kUnityIncQ12 * 2);
	CHECK_EQ(PitchIncQ12(48, 60), kUnityIncQ12 / 2);
	CHECK_EQ(PitchIncQ12(61, 60), (kUnityIncQ12 * kSemitoneSteps[1]) >> 12);
	// Clamped, never zero
	CHECK(PitchIncQ12(0, 127) > 0);
	CHECK(PitchIncQ12(127, 0) > 0);
}

static void TestMelodicEnvelopeAndLevel()
{
	auto pcm = MakeConst(24000, 16000);
	Engine e;
	e.SetPercussive(false);
	e.SetInstrument(kPartLead, pcm.data(), (uint32_t)pcm.size(), 60, false);
	e.NoteOn(kPartLead, 60, 127);
	CHECK_EQ(e.ActiveVoices(), 1);

	int32_t a = 0, b = 0;
	// Attack is ~2 ms = 96 samples; after 200 samples we're at sustain
	int32_t first = 0;
	for (int i = 0; i < 200; i++) {
		e.Render(a, b);
		if (i == 0) first = a;
	}
	// Attack starts near zero
	CHECK(first < 600);
	// Sustain level: 16000 * (4064/4096) * (2048/4096) ~= 7937
	CHECK(a > 7300 && a < 8600);
	CHECK_EQ(b, 0); // nothing on the bass bus

	// Release: 60 ms default -> gone within ~70 ms
	e.NoteOff(kPartLead, 60);
	for (int i = 0; i < 70 * 48; i++) e.Render(a, b);
	CHECK_EQ(e.ActiveVoices(), 0);
	CHECK_EQ(a, 0);
}

static void TestPitchConsumptionRate()
{
	auto pcm = MakeConst(2400, 8000); // 0.1 s at 24 kHz
	Engine e;
	e.SetPercussive(false);
	e.SetInstrument(kPartLead, pcm.data(), (uint32_t)pcm.size(), 60, false);

	// At root: 2400 frames / 0.5 per sample = 4800 output samples
	e.NoteOn(kPartLead, 60, 127);
	int32_t a, b;
	int samples = 0;
	while (e.ActiveVoices() > 0 && samples < 20000) { e.Render(a, b); samples++; }
	CHECK(samples > 4700 && samples < 4900);

	// One octave up: twice as fast
	e.NoteOn(kPartLead, 72, 127);
	samples = 0;
	while (e.ActiveVoices() > 0 && samples < 20000) { e.Render(a, b); samples++; }
	CHECK(samples > 2300 && samples < 2500);
}

static void TestAllocationCaps()
{
	auto pcm = MakeConst(24000, 8000);
	Engine e;
	e.SetPercussive(false);
	e.SetInstrument(kPartLead, pcm.data(), (uint32_t)pcm.size(), 60, true);
	e.SetInstrument(kPartBass, pcm.data(), (uint32_t)pcm.size(), 36, true);
	e.SetInstrument(kPartPad, pcm.data(), (uint32_t)pcm.size(), 60, true);

	// Duophonic lead: extra notes steal within the part
	for (int n = 0; n < 10; n++) e.NoteOn(kPartLead, 40 + n, 100);
	CHECK(e.ActiveVoices() <= Engine::kMaxLead);

	// Monophonic bass adds at most one more voice
	for (int n = 0; n < 6; n++) e.NoteOn(kPartBass, 30 + n, 100);
	CHECK(e.ActiveVoices() <= Engine::kMaxLead + Engine::kMaxBass);

	// 3-voice pad on top; total held stays within lead+bass+pad = 6
	for (int n = 0; n < 5; n++) e.NoteOn(kPartPad, 60 + n, 90);
	CHECK(e.ActiveVoices() <= Engine::kMaxLead + Engine::kMaxBass + Engine::kMaxPad);
	CHECK(e.ActiveVoices() <= Engine::kMaxVoices);

	e.KillAll();
	CHECK_EQ(e.ActiveVoices(), 0);
}

static void TestPadRoutingAndCap()
{
	auto pcm = MakeConst(24000, 8000);
	Engine e;
	e.SetPercussive(false);
	e.SetInstrument(kPartPad, pcm.data(), (uint32_t)pcm.size(), 60, true);

	// A full triad holds three voices; a fourth note steals one
	e.NoteOn(kPartPad, 60, 100);
	e.NoteOn(kPartPad, 63, 100);
	e.NoteOn(kPartPad, 67, 100);
	CHECK_EQ(e.ActiveVoices(), 3);
	e.NoteOn(kPartPad, 70, 100);
	CHECK_EQ(e.ActiveVoices(), 3);

	// Pad renders on out A, nothing on the bass bus
	int32_t a = 0, b = 0;
	for (int i = 0; i < 200; i++) e.Render(a, b);
	CHECK(a > 0);
	CHECK_EQ(b, 0);
}

static void TestDrumsAndChoke()
{
	auto kick = MakeConst(2400, 12000);
	auto ohat = MakeConst(24000, 9000);
	auto chat = MakeConst(1200, 9000);
	Engine e;
	e.SetPercussive(true);
	e.SetDrumLane(0, kick.data(), (uint32_t)kick.size(), 0);
	e.SetDrumLane(4, chat.data(), (uint32_t)chat.size(), 1); // closed hat
	e.SetDrumLane(6, ohat.data(), (uint32_t)ohat.size(), 1); // open hat

	// Open hat rings...
	e.DrumTrigger(6, 127);
	int32_t a, b;
	for (int i = 0; i < 480; i++) e.Render(a, b);
	CHECK_EQ(e.ActiveVoices(), 1);

	// ...until the closed hat chokes it (5 ms fast release + its own decay)
	e.DrumTrigger(4, 127);
	for (int i = 0; i < 480; i++) e.Render(a, b); // 10 ms
	// Open hat must be gone or releasing; after the closed hat's 1200
	// frames (2400 output samples) everything is silent
	for (int i = 0; i < 3000; i++) e.Render(a, b);
	CHECK_EQ(e.ActiveVoices(), 0);

	// One-shots end on their own (kick: 2400 frames -> 4800 samples)
	e.DrumTrigger(0, 100);
	int samples = 0;
	while (e.ActiveVoices() > 0 && samples < 6000) { e.Render(a, b); samples++; }
	CHECK(samples > 4700 && samples < 5000);

	// Melodic calls are ignored in percussive mode
	e.NoteOn(kPartLead, 60, 100);
	CHECK_EQ(e.ActiveVoices(), 0);
}

// Choke group 0 means NONE: pads without a group must never cut each
// other (field report: the UI showed "0" as if it were a shared group).
static void TestChokeZeroIsNone()
{
	auto a = MakeConst(24000, 9000);
	auto b = MakeConst(24000, 9000);
	Engine e;
	e.SetPercussive(true);
	e.SetDrumLane(0, a.data(), (uint32_t)a.size(), 0);
	e.SetDrumLane(2, b.data(), (uint32_t)b.size(), 0);

	e.DrumTrigger(0, 127);
	int32_t oa, ob;
	for (int i = 0; i < 480; i++) e.Render(oa, ob);
	e.DrumTrigger(2, 127);
	for (int i = 0; i < 480; i++) e.Render(oa, ob);
	// Both one-shots still sounding: no cross-choke happened
	CHECK_EQ(e.ActiveVoices(), 2);

	// Retriggering the SAME lane still replaces its own voice
	e.DrumTrigger(2, 127);
	for (int i = 0; i < 480; i++) e.Render(oa, ob);
	CHECK(e.ActiveVoices() <= 3); // old lane-2 voice is in fast release
}

static void TestOut2Submix()
{
	auto kick = MakeConst(24000, 10000);
	auto snare = MakeConst(24000, 10000);
	Engine e;
	e.SetPercussive(true);
	e.SetDrumLane(0, kick.data(), (uint32_t)kick.size(), 0);
	e.SetDrumLane(2, snare.data(), (uint32_t)snare.size(), 0);
	e.SetOut2LaneMask(0x0001); // only lane 0 on out B

	e.DrumTrigger(0, 127);
	e.DrumTrigger(2, 127);
	int32_t a = 0, b = 0;
	for (int i = 0; i < 200; i++) e.Render(a, b);
	// A carries both lanes, B only the kick: roughly half
	CHECK(a > b);
	CHECK(b > 0);
	CHECK(a > b + b / 2);
}

static void TestMelodicLoopSustain()
{
	auto pcm = MakeConst(1000, 8000);
	Engine e;
	e.SetPercussive(false);
	e.SetInstrument(kPartLead, pcm.data(), (uint32_t)pcm.size(), 60, true); // loop

	e.NoteOn(kPartLead, 60, 127);
	int32_t a, b;
	// 1000 frames = 2000 samples un-looped; loop keeps it alive far longer
	for (int i = 0; i < 20000; i++) e.Render(a, b);
	CHECK_EQ(e.ActiveVoices(), 1);
	e.NoteOff(kPartLead, 60);
	for (int i = 0; i < 70 * 48; i++) e.Render(a, b);
	CHECK_EQ(e.ActiveVoices(), 0);
}

int main()
{
	TestPitchTable();
	TestMelodicEnvelopeAndLevel();
	TestPitchConsumptionRate();
	TestAllocationCaps();
	TestPadRoutingAndCap();
	TestDrumsAndChoke();
	TestChokeZeroIsNone();
	TestOut2Submix();
	TestMelodicLoopSustain();
	return TestResult("test_engine");
}
