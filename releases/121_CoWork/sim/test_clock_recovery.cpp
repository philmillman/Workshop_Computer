// Clock recovery DLL under clean and jittered MIDI clock, tempo changes,
// and clock-loss detection. Simulates the real usage: one OnClock per
// received 0xF8 (with the local phase at that moment), TickIncQ16 applied
// every 48 kHz sample.

#include "test_common.h"
#include "../src/seq/ClockRecovery.h"

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

int main()
{
	TestCleanLock();
	TestJitteredLock();
	TestTempoChange();
	TestResyncDetect();
	TestClockLoss();
	TestOutlierRejection();
	return TestResult("test_clock_recovery");
}
