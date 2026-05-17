/*
 * placeholder_spatial.h — 3-parallel-delay-line spatial effect
 *
 * Implements the topology of the Fairfield Circuitry Placeholder (EB revision):
 * three delay taps drawn from the same circular buffer at golden-ratio-derived
 * time relationships, panned to create a stereo spatial echo effect.
 *
 * Signal flow:
 *
 *                         ┌────────────────────────────────────────────────┐
 *                         │                                                │
 *  stereo in ──(L+R/2)──► write ──► [Circular buffer, 32 KB mono]         │
 *                                      │            │            │         │
 *                                    tap 1        tap 2        tap 3       │
 *                                  T (long)    T × 0.618    T × 0.382      │
 *                                    Pan L      Pan Ctr      Pan R          │
 *                                      │            │            │         │
 *                                      └─────── sum / 3 ────────┘         │
 *                                                   │                      │
 *                                                [HPF] ──► × feedback ─────┘
 *
 * With spread = 0 all three taps are centred (mono echo).
 * With spread = 4096 tap 1 is hard-left and tap 3 is hard-right.
 *
 * Memory: int16_t buf[16384] = 32 KB (no heap needed)
 *
 * All arithmetic uses int32_t for fast RP2040 Cortex-M0+ execution.
 */

#pragma once
#include <stdint.h>
#include <string.h>

class PlaceholderSpatial
{
public:
	// Buffer size: power of 2 for fast bitmask modulo.
	// 16384 × 2 bytes = 32 KB, covers 0–341 ms @ 48 kHz.
	static constexpr int     BUF_SIZE  = 16384;
	static constexpr int     BUF_MASK  = BUF_SIZE - 1;

	// Maximum primary tap length accessible via the Time knob (250 ms @ 48 kHz)
	static constexpr int32_t TIME_MAX  = 12000;

	// Tap-2 and tap-3 golden-ratio multipliers (×1000 integer arithmetic)
	// φ^-1 ≈ 0.618 03,  φ^-2 ≈ 0.381 97
	static constexpr int32_t TAP2_NUM  = 618;
	static constexpr int32_t TAP3_NUM  = 382;

	PlaceholderSpatial()
	{
		memset(buf_, 0, sizeof(buf_));
		writePos_  = 0;
		tapLen_    = 4800;   // default 100 ms
		targetLen_ = 4800;
		feedback_  = 24576;  // ~75 % of 32768
		spread_    = 4096;   // default: full stereo pan
		hpState_   = 0;
		hpPrev_    = 0;
	}

	// Set primary tap length in samples.  Changes are slewed to avoid clicks.
	void __not_in_flash_func(setTime)(int32_t samples)
	{
		if (samples < 2)        samples = 2;
		if (samples > TIME_MAX) samples = TIME_MAX;
		targetLen_ = samples;
	}

	// Set feedback/regeneration.  0–32767; values > 31130 (~95 %) are clamped.
	void __not_in_flash_func(setFeedback)(int32_t fb)
	{
		if (fb < 0)     fb = 0;
		if (fb > 31130) fb = 31130;
		feedback_ = fb;
	}

	// Set stereo spread.  0 = mono (all taps centred), 4096 = full pan.
	void __not_in_flash_func(setSpread)(int32_t spr)
	{
		if (spr < 0)    spr = 0;
		if (spr > 4096) spr = 4096;
		spread_ = spr;
	}

	// Process one stereo sample pair.
	// inL / inR : −2048..2047 (12-bit ComputerCard ADC range)
	// outL / outR : three-tap wet signal, same range
	void __not_in_flash_func(process)(
		int32_t inL, int32_t inR,
		int32_t &outL, int32_t &outR)
	{
		// Slew primary tap one sample per call to avoid clicks on tempo changes
		if (tapLen_ < targetLen_) ++tapLen_;
		else if (tapLen_ > targetLen_) --tapLen_;

		// Compute tap 2 and tap 3 lengths from golden-ratio multipliers
		int32_t t2 = (tapLen_ * TAP2_NUM + 500) / 1000;
		int32_t t3 = (tapLen_ * TAP3_NUM + 500) / 1000;
		if (t2 < 1) t2 = 1;
		if (t3 < 1) t3 = 1;

		// Read three taps from the circular buffer
		int32_t d1 = (int32_t)buf_[(writePos_ - tapLen_) & BUF_MASK];
		int32_t d2 = (int32_t)buf_[(writePos_ - t2) & BUF_MASK];
		int32_t d3 = (int32_t)buf_[(writePos_ - t3) & BUF_MASK];

		// Mix the three taps for the feedback signal (÷3 maintains headroom)
		int32_t tapSum = (d1 + d2 + d3) / 3;

		// 1-pole HPF on feedback path — prevents DC build-up from accumulating
		// in the buffer.  Cutoff ≈ 32 Hz @ 48 kHz: α = 32639/32768.
		//   y[n] = x[n] − x[n−1] + α · y[n−1]
		int32_t hpOut = tapSum - hpPrev_ + ((hpState_ * 32639) >> 15);
		hpPrev_  = tapSum;
		hpState_ = hpOut;

		// Feedback contribution
		int32_t fbSig = (hpOut * feedback_) >> 15;

		// Sum stereo input to mono and add feedback
		int32_t writeVal = ((inL + inR) >> 1) + fbSig;

		// Saturate to int16 range before storing
		if (writeVal >  2047) writeVal =  2047;
		if (writeVal < -2048) writeVal = -2048;
		buf_[writePos_] = (int16_t)writeVal;
		writePos_ = (writePos_ + 1) & BUF_MASK;

		// Pan three taps to stereo output.
		//
		// panA = 4096 + spread_  (4096..8192) — the "near-left"  weight for tap 1
		// panB = 4096 − spread_  (0..4096)    — the "near-right" weight for tap 1
		// panC = 4096            (constant)   — centre weight for tap 2
		//
		// outL = (d1·panA + d2·panC + d3·panB) / 8192
		// outR = (d1·panB + d2·panC + d3·panA) / 8192
		//
		// At spread = 4096: tap 1 is 100 % L / 0 % R, tap 3 is 0 % L / 100 % R.
		// At spread = 0:    all taps are 50 % L / 50 % R (mono).
		int32_t panA = 4096 + spread_;
		int32_t panB = 4096 - spread_;

		outL = (d1 * panA + d2 * 4096 + d3 * panB) >> 13;
		outR = (d1 * panB + d2 * 4096 + d3 * panA) >> 13;
	}

private:
	int16_t  buf_[BUF_SIZE];  // 32 KB mono circular buffer

	int      writePos_;
	int32_t  tapLen_;     // current primary tap length (sample-slewed)
	int32_t  targetLen_;  // target primary tap length
	int32_t  feedback_;   // 0–32767  (~95 % max = 31130)
	int32_t  spread_;     // 0 = mono, 4096 = full stereo pan

	// HPF state on feedback path
	int32_t  hpState_;
	int32_t  hpPrev_;
};
