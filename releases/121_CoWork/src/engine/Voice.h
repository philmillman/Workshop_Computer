// One sample-playback voice: Q20.12 phase accumulator, linear interpolation,
// Q15 linear AR envelope. int32 fixed point throughout — no float in the
// audio path. Pure header, host-testable (data pointer is XIP flash on
// hardware, plain RAM in tests).

#ifndef COWORK_VOICE_H
#define COWORK_VOICE_H

#include <cstdint>

namespace cowork {

constexpr int kPhaseFracBits = 12;
constexpr int32_t kEnvOne = 32768; // Q15
constexpr int32_t kAttackStep = kEnvOne / 96;   // ~2 ms at 48 kHz
constexpr int32_t kChokeStep = kEnvOne / 240;   // ~5 ms fast release

struct Voice {
	const int16_t *data = nullptr;
	uint32_t frames = 0;
	uint32_t phaseQ12 = 0;
	uint32_t incQ12 = 0;
	int32_t env = 0;
	int32_t releaseStep = kEnvOne / 480; // ~10 ms default
	int32_t gainQ12 = 0;
	uint32_t seq = 0;      // allocation order, for oldest-voice stealing
	uint8_t state = 0;     // 0 idle, 1 attack, 2 sustain, 3 release
	uint8_t part = 0;      // kPartLead / kPartBass / kPartDrums
	uint8_t note = 0;
	uint8_t lane = 0xFF;   // drum lane, 0xFF for melodic
	uint8_t chokeGroup = 0;
	bool loop = false;

	bool Active() const { return state != 0; }
	bool Held() const { return state == 1 || state == 2; }

	void Start(const int16_t *d, uint32_t nFrames, uint32_t inc, int32_t gain)
	{
		data = d;
		frames = nFrames;
		phaseQ12 = 0;
		incQ12 = inc;
		env = 0;
		gainQ12 = gain;
		state = 1;
	}

	void Release() { if (Held()) state = 3; }
	void Choke() { if (Active()) { state = 3; releaseStep = kChokeStep; } }
	void Kill() { state = 0; }

	// Advance one output sample; returns the enveloped, velocity-scaled
	// sample in roughly int16 range.
	int32_t Render()
	{
		if (state == 0) return 0;

		uint32_t idx = phaseQ12 >> kPhaseFracBits;
		if (idx + 1 >= frames) {
			// End of data: melodic loop wraps to the second half
			// (VSS-style sustain); one-shots and releases just end.
			if (loop && Held() && frames >= 4) {
				phaseQ12 = (frames / 2) << kPhaseFracBits;
				idx = frames / 2;
			} else {
				state = 0;
				return 0;
			}
		}

		int32_t frac = (int32_t)(phaseQ12 & ((1u << kPhaseFracBits) - 1));
		int32_t s0 = data[idx];
		int32_t s1 = data[idx + 1];
		int32_t s = s0 + (((s1 - s0) * frac) >> kPhaseFracBits);
		phaseQ12 += incQ12;

		if (state == 1) {
			env += kAttackStep;
			if (env >= kEnvOne) { env = kEnvOne; state = 2; }
		} else if (state == 3) {
			env -= releaseStep;
			if (env <= 0) { env = 0; state = 0; return 0; }
		}

		return (((s * env) >> 15) * gainQ12) >> 12;
	}
};

} // namespace cowork

#endif
