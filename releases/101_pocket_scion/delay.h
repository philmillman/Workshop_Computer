/*
 * delay.h — Stereo delay DSP for Pocket Scion card
 *
 * Two circular ring buffers (left + right), with feedback and a
 * 1-pole high-pass filter on the feedback path to prevent DC build-up.
 *
 * All arithmetic uses int32_t to stay fast on the RP2040 Cortex-M0+.
 * Buffer samples are stored as int16_t to keep RAM usage manageable.
 */

#pragma once
#include <stdint.h>
#include <string.h>

class StereoDelay
{
public:
	// 250ms at 48 kHz = 12 000 samples per channel
	// Two channels × 12 000 × 2 bytes = 48 KB total
	static constexpr int DELAY_MAX = 12000;

	StereoDelay()
	{
		memset(bufL, 0, sizeof(bufL));
		memset(bufR, 0, sizeof(bufR));
		writePos      = 0;
		delayLen      = 4800;      // default 100 ms
		targetDelayLen = 4800;
		feedback      = 24576;     // ~75 % of 32768
		hpStateL      = 0;
		hpStateR      = 0;
		hpPrevL       = 0;
		hpPrevR       = 0;
	}

	// Set delay time in samples.  Changes are slewed each process() call.
	void __not_in_flash_func(setDelayTime)(int32_t samples)
	{
		if (samples < 1)          samples = 1;
		if (samples >= DELAY_MAX) samples = DELAY_MAX - 1;
		targetDelayLen = samples;
	}

	// Set feedback amount.  Max ~95 % to prevent runaway.
	// Accepts 0–32767; values > 31130 are clamped (~95 %).
	void __not_in_flash_func(setFeedback)(int32_t fb)
	{
		if (fb < 0)     fb = 0;
		if (fb > 31130) fb = 31130;
		feedback = fb;
	}

	// Process one stereo sample.
	// inL/inR: -2048..2047  (12-bit ADC range from ComputerCard)
	// outL/outR: delayed signal, same range
	void __not_in_flash_func(process)(int32_t inL, int32_t inR,
	                                   int32_t &outL, int32_t &outR)
	{
		// Slew delay length one sample at a time to avoid clicks on tempo change
		if (delayLen < targetDelayLen) ++delayLen;
		else if (delayLen > targetDelayLen) --delayLen;

		// Compute read position (circular, always positive)
		int rp = writePos - delayLen;
		if (rp < 0) rp += DELAY_MAX;

		int32_t delayedL = (int32_t)bufL[rp];
		int32_t delayedR = (int32_t)bufR[rp];

		// 1-pole high-pass filter on feedback path: y[n] = x[n] - x[n-1] + α·y[n-1]
		// α ≈ 0.9958 → -(1 - 0.9958) = −0.0042, f_c ≈ 32 Hz at 48 kHz.
		// Stored as alpha = 32639/32768 (integer shift approximation).
		int32_t fbL = delayedL - hpPrevL + ((hpStateL * 32639) >> 15);
		int32_t fbR = delayedR - hpPrevR + ((hpStateR * 32639) >> 15);
		hpPrevL  = delayedL;
		hpPrevR  = delayedR;
		hpStateL = fbL;
		hpStateR = fbR;

		// Mix dry input with filtered feedback and write into buffer
		int32_t writeL = inL + ((fbL * feedback) >> 15);
		int32_t writeR = inR + ((fbR * feedback) >> 15);

		// Clamp to int16 range before storing
		if (writeL >  2047) writeL =  2047;
		if (writeL < -2048) writeL = -2048;
		if (writeR >  2047) writeR =  2047;
		if (writeR < -2048) writeR = -2048;

		bufL[writePos] = (int16_t)writeL;
		bufR[writePos] = (int16_t)writeR;

		++writePos;
		if (writePos >= DELAY_MAX) writePos = 0;

		outL = delayedL;
		outR = delayedR;
	}

private:
	int16_t bufL[DELAY_MAX];
	int16_t bufR[DELAY_MAX];

	int     writePos;
	int     delayLen;
	int     targetDelayLen;
	int32_t feedback;    // 0–32767

	// High-pass filter state
	int32_t hpStateL, hpStateR;  // filter output
	int32_t hpPrevL,  hpPrevR;   // previous delayed sample (filter input)
};
