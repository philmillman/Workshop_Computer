// Follower clock recovery: locks the local 96 PPQN phase to received MIDI
// clock (0xF8 at 24 PPQN, so 4 local ticks per clock). A simple software
// DLL: EMA period estimate plus a phase correction snapshotted at each
// clock arrival and spread over the following interval — sized to absorb
// the ~1 ms USB full-speed framing jitter.
// Pure header, host-testable (sim/test_clock_recovery.cpp).

#ifndef COWORK_CLOCK_RECOVERY_H
#define COWORK_CLOCK_RECOVERY_H

#include <cstdint>

namespace cowork {

class ClockRecovery {
public:
	static constexpr uint32_t kTicksPerClock = 4; // 96 / 24
	static constexpr uint32_t kMinIntervalUs = 2000;    // > 1250 BPM: reject
	static constexpr uint32_t kMaxIntervalUs = 250000;  // < 10 BPM: reject
	static constexpr uint32_t kStopUs = 2000000;        // silence -> stopped

	void Reset(uint32_t bpmX10)
	{
		// period per 24 PPQN clock = 60e6 / (bpm * 24) us
		periodEstUs_ = (uint32_t)(600000000ull / ((uint64_t)bpmX10 * 24u));
		haveLast_ = false;
		rxCount_ = 0;
		lastClockUs_ = 0;
		corrQ16_ = 0;
		lastErrQ16_ = 0;
	}

	// A 0xFA restarts tick counting from zero (first 0xF8 = tick 0).
	void OnStart()
	{
		rxCount_ = 0;
		haveLast_ = false;
		corrQ16_ = 0;
		lastErrQ16_ = 0;
	}

	// Called when a 0xF8 is consumed. localPhaseQ16 is the sequencer
	// phase at that moment; the phase error is snapshotted here and bled
	// off over the next clock interval.
	void OnClock(uint32_t tUs, uint64_t localPhaseQ16)
	{
		if (haveLast_) {
			uint32_t interval = tUs - lastUs_;
			if (interval >= kMinIntervalUs && interval <= kMaxIntervalUs)
				periodEstUs_ += ((int32_t)interval - (int32_t)periodEstUs_) >> 3;
		}
		lastUs_ = tUs;
		lastClockUs_ = tUs;
		haveLast_ = true;
		rxCount_++;

		int64_t err = (int64_t)TargetPhaseQ16() - (int64_t)localPhaseQ16;
		lastErrQ16_ = err;

		uint32_t spc = SamplesPerClock();
		int32_t corr = (int32_t)(err / (int64_t)spc);
		int32_t limit = (int32_t)(NominalIncQ16() * 3u / 100u);
		if (limit < 1) limit = 1;
		if (corr > limit) corr = limit;
		if (corr < -limit) corr = -limit;
		corrQ16_ = corr;
	}

	uint32_t RxCount() const { return rxCount_; }
	uint32_t PeriodUs() const { return periodEstUs_; }
	uint32_t LastClockUs() const { return lastClockUs_; }
	bool HasClock() const { return rxCount_ > 0; }

	// Estimated leader tempo for display, BPM x10.
	uint32_t BpmX10() const
	{
		if (periodEstUs_ == 0) return 1200;
		return (uint32_t)(600000000ull / ((uint64_t)periodEstUs_ * 24u));
	}

	// Target local phase (Q16 ticks) implied by the received clock count.
	// The first 0xF8 after Start marks tick 0 (standard MIDI), so N
	// received clocks put the leader at tick (N-1)*4.
	uint64_t TargetPhaseQ16() const
	{
		if (rxCount_ == 0) return 0;
		return ((uint64_t)(rxCount_ - 1) * kTicksPerClock) << 16;
	}

	// True if, at the last clock, the local phase was off by more than a
	// beat: the caller should SeekTick(TargetPhaseQ16()) + OnResync().
	bool NeedsResync() const
	{
		int64_t e = lastErrQ16_ < 0 ? -lastErrQ16_ : lastErrQ16_;
		return e > ((int64_t)96 << 16);
	}

	void OnResync()
	{
		lastErrQ16_ = 0;
		corrQ16_ = 0;
	}

	// Q16 tick increment per 48 kHz sample.
	uint32_t TickIncQ16() const
	{
		return (uint32_t)((int32_t)NominalIncQ16() + corrQ16_);
	}

	// Clock-loss states, given the current time.
	bool Freewheeling(uint32_t nowUs) const
	{
		return HasClock() && (nowUs - lastClockUs_) > periodEstUs_ * 4u;
	}
	bool TimedOut(uint32_t nowUs) const
	{
		return HasClock() && (nowUs - lastClockUs_) > kStopUs;
	}

private:
	uint32_t SamplesPerClock() const
	{
		uint32_t spc = (periodEstUs_ * 48u) / 1000u;
		return spc < 96 ? 96 : spc;
	}

	uint32_t NominalIncQ16() const
	{
		return (uint32_t)(((uint64_t)kTicksPerClock << 16) / SamplesPerClock());
	}

	uint32_t periodEstUs_ = 20833; // 120 BPM
	uint32_t lastUs_ = 0;
	uint32_t lastClockUs_ = 0;
	uint32_t rxCount_ = 0;
	int32_t corrQ16_ = 0;
	int64_t lastErrQ16_ = 0;
	bool haveLast_ = false;
};

} // namespace cowork

#endif
