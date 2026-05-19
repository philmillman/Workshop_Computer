/*
 * placeholder_reverb.h — Placeholder (EB) reverb (feedback delay network)
 *
 * Digital emulation of the Fairfield Circuitry Placeholder (EB revision) per
 * EB_Specifications.pdf: three independent delay lines, Householder cross-
 * feedback, fourth in-phase TONE path, and digital delay-clock modulation.
 */

#pragma once
#include <stdint.h>
#include <string.h>

class PlaceholderReverb
{
public:
	enum class ModMode : uint8_t
	{
		None      = 0,
		Cyclical  = 1,
		Random    = 2,
		Both      = 3,
	};

	static constexpr int     BUF_SIZE  = 8192;
	static constexpr int     BUF_MASK  = BUF_SIZE - 1;

	static constexpr int32_t TIME_MIN  = 576;   // 12 ms @ 48 kHz
	static constexpr int32_t TIME_MAX  = 7680;  // 160 ms

	// One-pole LP coefficient for ~500 Hz pivot (EB TONE spec)
	static constexpr int32_t TONE_LP_K = 30720;

	void __not_in_flash_func(resetToneFilter)()
	{
		lpState_ = 0;
	}

	PlaceholderReverb()
	{
		memset(buf_, 0, sizeof(buf_));
		for (int i = 0; i < 3; ++i)
		{
			writePos_[i]  = 0;
			len_[i]       = 4800;
			targetLen_[i] = 4800;
			modPhase_[i]  = 0;
			modRand_[i]   = 0;
			modRandT_[i]  = 0;
		}
		decay_       = 24576;
		ratio_       = 0;
		spread_      = 2048;
		tone_        = 0;
		modDepth_    = 0;
		modMode_     = ModMode::None;
		modLpAlpha_  = 0;   // 0 = open (no LPF on mod)
		hpState_     = 0;
		hpPrev_      = 0;
		lpState_     = 0;
		updateRatioTargets();
		for (int i = 0; i < 3; ++i)
			len_[i] = targetLen_[i];
	}

	void __not_in_flash_func(setTime)(int32_t samples)
	{
		if (samples < TIME_MIN)  samples = TIME_MIN;
		if (samples > TIME_MAX)  samples = TIME_MAX;
		targetLen_[0] = samples;
		updateRatioTargets();
	}

	void __not_in_flash_func(setRatio)(int32_t ratio)
	{
		if (ratio < -4096) ratio = -4096;
		if (ratio >  4096) ratio =  4096;
		ratio_ = ratio;
		updateRatioTargets();
	}

	void __not_in_flash_func(setFeedback)(int32_t fb)
	{
		if (fb < 0)     fb = 0;
		if (fb > 31130) fb = 31130;
		decay_ = fb;
	}

	void __not_in_flash_func(setTone)(int32_t tone)
	{
		if (tone < -4096) tone = -4096;
		if (tone >  4096) tone =  4096;
		tone_ = tone;
	}

	// MOD DEPTH — subtle delay-time modulation (0 = off).
	void __not_in_flash_func(setModDepth)(int32_t depth)
	{
		if (depth < 0)    depth = 0;
		if (depth > 4096) depth = 4096;
		modDepth_ = depth;
	}

	// MOD TYPE — knob 0..4095 maps to none / cyclical / random / both.
	void __not_in_flash_func(setModType)(int32_t typeKnob)
	{
		if (typeKnob < 0) typeKnob = 0;
		if (typeKnob > 4095) typeKnob = 4095;
		if (typeKnob < 1024)
			modMode_ = ModMode::None;
		else if (typeKnob < 2048)
			modMode_ = ModMode::Cyclical;
		else if (typeKnob < 3072)
			modMode_ = ModMode::Random;
		else
			modMode_ = ModMode::Both;
	}

	// MOD LPF — 1st-order cutoff on modulation (pedal: 2k / 4k / 8k / open).
	void __not_in_flash_func(setModLpCutoff)(int32_t cutoffKnob)
	{
		if (cutoffKnob < 0) cutoffKnob = 0;
		if (cutoffKnob > 4095) cutoffKnob = 4095;
		// 0 = 2 kHz … 4095 = open
		if (cutoffKnob > 3400)
			modLpAlpha_ = 0;
		else if (cutoffKnob > 2270)
			modLpAlpha_ = 32256;  // ~8 kHz
		else if (cutoffKnob > 1135)
			modLpAlpha_ = 32480;  // ~4 kHz
		else
			modLpAlpha_ = 32608;  // ~2 kHz
	}

	void __not_in_flash_func(process)(
		int32_t inL, int32_t inR,
		int32_t &outL, int32_t &outR)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (len_[i] < targetLen_[i]) ++len_[i];
			else if (len_[i] > targetLen_[i]) --len_[i];
		}

		int32_t modOff[3];
		computeModOffsets(modOff);

		int32_t d[3];
		for (int i = 0; i < 3; ++i)
		{
			int32_t readLen = len_[i] + modOff[i];
			if (readLen < TIME_MIN) readLen = TIME_MIN;
			if (readLen > TIME_MAX) readLen = TIME_MAX;
			d[i] = readDelay(i, readLen);
		}

		int32_t mono = (inL + inR) >> 1;
		int32_t hpIn = mono - hpPrev_ + ((hpState_ * 32639) >> 15);
		hpPrev_  = mono;
		hpState_ = hpIn;

		int32_t wetCore = (d[0] + d[1] + d[2]) / 3;
		int32_t toned   = applyTone(wetCore);
		int32_t wet     = toned;
		// In-phase path: pre-tone sum keeps feedback stable; tone shapes the
		// audible wet output (pedal TONE is shelving, not a full-band swap).
		int32_t global  = (wetCore * decay_) >> 16;

		int32_t fb[3];
		fb[0] = (((d[1] + d[2]) >> 1) * decay_) >> 15;
		fb[1] = (((d[0] + d[2]) >> 1) * decay_) >> 15;
		fb[2] = (((d[0] + d[1]) >> 1) * decay_) >> 15;

		for (int i = 0; i < 3; ++i)
		{
			int32_t w = hpIn + fb[i] + global;
			writeDelay(i, softLimit(w));
		}

		int32_t width = ((d[0] - d[2]) * spread_) >> 13;
		outL = softLimit(wet + width);
		outR = softLimit(wet - width);
	}

private:
	void updateRatioTargets()
	{
		int32_t size = targetLen_[0];
		int32_t d2 = size + ((size * ratio_) >> 13);
		int32_t d3 = size + ((size * ratio_ * 3) >> 14);
		if (d2 < TIME_MIN) d2 = TIME_MIN;
		if (d3 < TIME_MIN) d3 = TIME_MIN;
		if (d2 > TIME_MAX) d2 = TIME_MAX;
		if (d3 > TIME_MAX) d3 = TIME_MAX;
		targetLen_[1] = d2;
		targetLen_[2] = d3;
	}

	void __not_in_flash_func(computeModOffsets)(int32_t *modOff)
	{
		modOff[0] = modOff[1] = modOff[2] = 0;
		if (modMode_ == ModMode::None || modDepth_ == 0)
			return;

		for (int i = 0; i < 3; ++i)
		{
			int32_t cyclic = 0;
			int32_t random = 0;

			if (modMode_ == ModMode::Cyclical || modMode_ == ModMode::Both)
			{
				// Rate ∝ 1/delay (EB cyclical mode).
				uint32_t step = (uint32_t)(48000 / (len_[i] + 1));
				if (step < 8) step = 8;
				modPhase_[i] += step;
				// Triangle LFO −32768..32767
				uint32_t p = modPhase_[i] >> 16;
				int32_t tri = (int32_t)(p & 0xFFFF);
				if (tri > 32767) tri = 65535 - tri;
				cyclic = (tri - 16384) >> 1;
			}

			if (modMode_ == ModMode::Random || modMode_ == ModMode::Both)
			{
				if (++modRandT_[i] > (uint32_t)(len_[i] << 2))
				{
					modRandT_[i] = 0;
					modRand_[i] = (int32_t)((modPhase_[i] ^ (modPhase_[i] >> 7)
					                         ^ (uint32_t)i * 1103515245u) & 0x3FFF)
					                - 8192;
				}
				random = modRand_[i] >> 2;
			}

			int32_t raw = cyclic + random;
			if (modLpAlpha_ != 0)
			{
				modLpState_[i] = raw + ((modLpState_[i] * modLpAlpha_) >> 15);
				raw = modLpState_[i];
			}
			else
			{
				modLpState_[i] = raw;
			}

			// Subtle depth: up to ~4 % of delay length at full depth.
			modOff[i] = (raw * modDepth_ * len_[i]) >> 26;
		}
	}

	int32_t __not_in_flash_func(readDelay)(int line, int32_t delayLen)
	{
		int rp = (writePos_[line] - delayLen) & BUF_MASK;
		return (int32_t)buf_[line][rp];
	}

	void __not_in_flash_func(writeDelay)(int line, int16_t sample)
	{
		buf_[line][writePos_[line]] = sample;
		writePos_[line] = (writePos_[line] + 1) & BUF_MASK;
	}

	static int16_t __not_in_flash_func(softLimit)(int32_t x)
	{
		// Gentle saturation before int16 delay storage (avoids hard clip hash)
		if (x > 2047)
		{
			int32_t over = x - 2047;
			x = 2047 + (over >> 2);
			if (x > 3071)
				x = 3071;
		}
		else if (x < -2048)
		{
			int32_t over = -2048 - x;
			x = -2048 - (over >> 2);
			if (x < -3072)
				x = -3072;
		}
		return (int16_t)x;
	}

	int32_t __not_in_flash_func(applyTone)(int32_t x)
	{
		// ~500 Hz one-pole split (EB: tilt centred at 500 Hz)
		lpState_ = x + ((lpState_ * TONE_LP_K) >> 15);
		int32_t lp = lpState_;
		int32_t hp = x - lp;

		if (tone_ == 0)
			return x;

		// Shelving tilt: CW cuts lows + boosts highs, CCW the reverse.
		// Limited to ~±3 dB so extremes stay musical, not full HP/LP swap.
		int32_t t = tone_;
		int32_t gLo = 4096 - (t >> 2);
		int32_t gHi = 4096 + (t >> 2);
		if (gLo < 2048)
			gLo = 2048;
		if (gHi > 6144)
			gHi = 6144;

		int32_t out = ((lp * gLo) + (hp * gHi)) >> 13;
		if (out > 2047)
			out = 2047;
		if (out < -2048)
			out = -2048;
		return out;
	}

	int16_t  buf_[3][BUF_SIZE];

	int      writePos_[3];
	int32_t  len_[3];
	int32_t  targetLen_[3];
	int32_t  decay_;
	int32_t  ratio_;
	int32_t  spread_;
	int32_t  tone_;

	int32_t  modDepth_;
	ModMode  modMode_;
	int32_t  modLpAlpha_;
	uint32_t modPhase_[3];
	int32_t  modRand_[3];
	uint32_t modRandT_[3];
	int32_t  modLpState_[3];

	int32_t  hpState_;
	int32_t  hpPrev_;
	int32_t  lpState_;
};
