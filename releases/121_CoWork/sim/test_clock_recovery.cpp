// Clock recovery DLL under clean and jittered MIDI clock, tempo changes,
// and clock-loss detection. Simulates the real usage: one OnClock per
// received 0xF8 (with the local phase at that moment), TickIncQ16 applied
// every 48 kHz sample.

#include "test_common.h"
#include "../src/seq/ClockRecovery.h"
#include "../src/seq/Sequencer.h"

using namespace cowork;

// Deterministic pseudo-random (LCG), no <random> needed
static uint32_t gRand = 12345;
static int32_t RandJitterUs(int32_t maxUs)
{
	gRand = gRand * 1664525u + 1013904223u;
	return (int32_t)(gRand % (2 * maxUs + 1)) - maxUs;
}

struct Sim {
	ClockRecovery cr;
	uint64_t phaseQ16 = 0;
	uint64_t timeNumUs48 = 0; // time * 48, so samples = time*48/1000 exactly
	uint64_t samplesRun = 0;
	uint32_t nowUs = 0;

	// Deliver one clock (with jitter), then run the audio samples that
	// elapse before the next clock.
	void RunInterval(uint32_t periodUs, int32_t jitterUs)
	{
		cr.OnClock((uint32_t)((int32_t)nowUs + jitterUs), phaseQ16);
		nowUs += periodUs;
		timeNumUs48 += (uint64_t)periodUs * 48;
		uint64_t targetSamples = timeNumUs48 / 1000;
		while (samplesRun < targetSamples) {
			phaseQ16 += cr.TickIncQ16();
			samplesRun++;
		}
	}

	// Error vs the true leader phase at "now" (just before the next
	// clock): the leader is at (rxCount-1)*4 + 4 ticks by then.
	double ErrTicks() const
	{
		uint64_t leader = cr.TargetPhaseQ16() + ((uint64_t)4 << 16);
		return ((double)(int64_t)(leader - phaseQ16)) / 65536.0;
	}
};

static void TestCleanLock()
{
	Sim sim;
	sim.cr.Reset(1200);
	sim.cr.OnStart();

	for (int i = 0; i < 200; i++)
		sim.RunInterval(20833, 0); // perfect 120 BPM

	CHECK(sim.cr.RxCount() == 200);
	double err = sim.ErrTicks();
	CHECK(err > -0.5 && err < 0.5);
	// Tempo estimate within 1%
	uint32_t bpm = sim.cr.BpmX10();
	CHECK(bpm > 1188 && bpm < 1212);
}

static void TestJitteredLock()
{
	Sim sim;
	sim.cr.Reset(1200);
	sim.cr.OnStart();

	double maxAbsErr = 0;
	for (int i = 0; i < 500; i++) {
		sim.RunInterval(20833, RandJitterUs(1000)); // +/-1 ms USB framing
		if (i > 50) {
			double e = sim.ErrTicks();
			if (e < 0) e = -e;
			if (e > maxAbsErr) maxAbsErr = e;
		}
	}
	// With +/-1 ms jitter on a 20.8 ms interval, stay within 2 ticks
	// (2 ticks = ~10 ms at 120 BPM; audible wobble would be far larger)
	CHECK(maxAbsErr < 2.0);
	CHECK(!sim.cr.NeedsResync());
}

static void TestTempoChange()
{
	Sim sim;
	sim.cr.Reset(1200);
	sim.cr.OnStart();

	for (int i = 0; i < 100; i++)
		sim.RunInterval(20833, 0); // 120 BPM
	for (int i = 0; i < 200; i++)
		sim.RunInterval(17857, 0); // -> 140 BPM

	uint32_t bpm = sim.cr.BpmX10();
	CHECK(bpm > 1385 && bpm < 1415);
	double err = sim.ErrTicks();
	if (err < 0) err = -err;
	CHECK(err < 4.0); // re-converged after the jump
	CHECK(!sim.cr.NeedsResync());
}

static void TestResyncDetect()
{
	ClockRecovery cr;
	cr.Reset(1200);
	cr.OnStart();
	uint32_t t = 0;
	// Local phase stuck at zero while 200 clocks arrive -> way off
	for (int i = 0; i < 200; i++) { t += 20833; cr.OnClock(t, 0); }
	CHECK(cr.NeedsResync());
	cr.OnResync();
	CHECK(!cr.NeedsResync());
}

static void TestClockLoss()
{
	ClockRecovery cr;
	cr.Reset(1200);
	uint32_t t = 1000;
	uint64_t phase = 0;
	for (int i = 0; i < 10; i++) { t += 20833; cr.OnClock(t, phase); phase += 4ull << 16; }
	CHECK(!cr.Freewheeling(t + 20833));
	CHECK(cr.Freewheeling(t + 5 * 20833));
	CHECK(!cr.TimedOut(t + 5 * 20833));
	CHECK(cr.TimedOut(t + 2100000));
}

static void TestOutlierRejection()
{
	ClockRecovery cr;
	cr.Reset(1200);
	uint32_t t = 0;
	uint64_t phase = 0;
	for (int i = 0; i < 50; i++) { t += 20833; cr.OnClock(t, phase); phase += 4ull << 16; }
	uint32_t before = cr.PeriodUs();
	// A giant gap (host stall) must not poison the estimate
	t += 1000000;
	cr.OnClock(t, phase);
	uint32_t after = cr.PeriodUs();
	CHECK_EQ(before, after);
}

// Field regression: the follower died after the first song-loop wrap.
// The received clock count is monotonic, but the sequencer phase folds at
// the loop — comparing them made the DLL see a whole-song error and fire
// a resync (killing all voices) on every clock. The fix runs the DLL in a
// monotonic clock-domain phase, exactly as wired here.
struct CountingListener : cowork::Sequencer::Listener {
	int ons = 0;
	void OnNoteOn(uint8_t, uint8_t, uint8_t) override { ons++; }
	void OnNoteOff(uint8_t, uint8_t) override {}
	void OnTempo(uint32_t) override {}
	void OnLoopWrap() override {}
};

// Play `clocks` MIDI clocks of a looping 2-bar song at 120 BPM through the
// follower wiring; feedFolded = the old, buggy comparison. Returns resyncs.
static int RunFollowerLoops(int clocks, bool feedFolded, int *onsOut)
{
	SongBuilder b;
	b.lengthTicks = 768; // 2 bars of 4/4
	b.loopEnd = 768;
	b.NoteOn(0, kPartDrums, 36, 100);
	auto img = b.Build();

	cowork::Sequencer seq;
	CHECK(seq.Attach(img.data(), (uint32_t)img.size()));
	ClockRecovery cr;
	cr.Reset(1200);
	cr.OnStart();

	CountingListener l;
	uint64_t ext = 0;
	uint64_t timeNum48 = 0, samplesRun = 0;
	uint32_t t = 0;
	int resyncs = 0;

	for (int c = 0; c < clocks; c++) {
		cr.OnClock(t, feedFolded ? seq.PhaseQ16() : ext);
		t += 20833;
		timeNum48 += 20833ull * 48;
		uint64_t targetSamples = timeNum48 / 1000;
		while (samplesRun < targetSamples) {
			if (cr.NeedsResync()) {
				resyncs++;
				ext = cr.TargetPhaseQ16();
				seq.SeekTick(ext);
				cr.OnResync();
			}
			uint32_t inc = cr.TickIncQ16();
			seq.SetTickIncQ16(inc);
			seq.Advance(l);
			ext += inc;
			samplesRun++;
		}
	}
	if (onsOut) *onsOut = l.ons;
	return resyncs;
}

static void TestFollowerAcrossLoopWraps()
{
	// 15 loops of the 2-bar song: 768 ticks / 4 = 192 clocks per loop
	int ons = 0;
	int resyncs = RunFollowerLoops(192 * 15, false, &ons);
	CHECK_EQ(resyncs, 0);
	CHECK_EQ(ons, 15); // the downbeat note fired on every single loop

	// The old wiring (folded phase into the DLL) storms with resyncs
	// after the first wrap — this is what killed the follower on hardware.
	int onsOld = 0;
	int resyncsOld = RunFollowerLoops(192 * 15, true, &onsOld);
	CHECK(resyncsOld > 10);
}

static void TestWarmupFromWrongDefault()
{
	// First-ever run: DLL seeded at 120 BPM default, leader plays 100 BPM.
	// The warm-up EMA must learn the real tempo within a few clocks.
	Sim sim;
	sim.cr.Reset(1200);
	sim.cr.OnStart();
	for (int i = 0; i < 16; i++)
		sim.RunInterval(25000, 0); // 100 BPM
	uint32_t bpm = sim.cr.BpmX10();
	CHECK(bpm > 990 && bpm < 1010);
	double err = sim.ErrTicks();
	if (err < 0) err = -err;
	CHECK(err < 2.0); // in step within two beats of the very first play
}

int main()
{
	TestCleanLock();
	TestFollowerAcrossLoopWraps();
	TestWarmupFromWrongDefault();
	TestJitteredLock();
	TestTempoChange();
	TestResyncDetect();
	TestClockLoss();
	TestOutlierRejection();
	return TestResult("test_clock_recovery");
}
