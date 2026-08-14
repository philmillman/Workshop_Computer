// 8-voice sample playback engine: melodic (lead + bass, pitched) or
// percussive (16-lane drum kit, one-shots with choke groups).
// Pure header, host-testable. The caller maps drum MIDI notes to lanes.

#ifndef COWORK_ENGINE_H
#define COWORK_ENGINE_H

#include <cstdint>
#include "Voice.h"
#include "PitchTable.h"
#include "../seq/SeqFormat.h"
#include "../storage/SampleBankFormat.h"

namespace cowork {

class Engine {
public:
	static constexpr int kMaxVoices = 8;
	// Per-part ceilings in melodic mode: duophonic lead, 3-voice pad,
	// monophonic bass (6 held voices max; the 2 spare pool slots absorb
	// release tails so stealing stays rare).
	static constexpr int kMaxLead = 2;
	static constexpr int kMaxBass = 1;
	static constexpr int kMaxPad = 3;

	// Part mix gains, Q12: lead -6 dB, bass -4.5 dB, pad -10 dB, drums -7 dB
	static constexpr int32_t kGainLead = 2048;
	static constexpr int32_t kGainBass = 2440;
	static constexpr int32_t kGainPad = 1290;
	static constexpr int32_t kGainDrums = 1830;

	struct Instrument {
		const int16_t *data = nullptr;
		uint32_t frames = 0;
		uint8_t root = 60;
		bool loop = false;
	};
	struct DrumLane {
		const int16_t *data = nullptr;
		uint32_t frames = 0;
		uint8_t choke = 0;
	};

	void SetPercussive(bool p) { percussive_ = p; }
	bool Percussive() const { return percussive_; }

	void SetInstrument(int part, const int16_t *data, uint32_t frames,
	                   uint8_t root, bool loop)
	{
		Instrument &ins = (part == kPartBass) ? bass_
		                : (part == kPartPad) ? pad_ : lead_;
		ins.data = data;
		ins.frames = frames;
		ins.root = root;
		ins.loop = loop;
	}

	void SetDrumLane(uint32_t lane, const int16_t *data, uint32_t frames, uint8_t choke)
	{
		if (lane >= kNumDrumLanes) return;
		lanes_[lane].data = data;
		lanes_[lane].frames = frames;
		lanes_[lane].choke = choke;
	}

	void ClearSamples()
	{
		KillAll();
		lead_ = Instrument{};
		bass_ = Instrument{};
		pad_ = Instrument{};
		for (auto &l : lanes_) l = DrumLane{};
	}

	void SetReleaseMs(int part, int ms)
	{
		if (ms < 1) ms = 1;
		int32_t step = kEnvOne / (ms * 48);
		if (step < 1) step = 1;
		if (part == kPartBass) releaseBass_ = step;
		else if (part == kPartPad) releasePad_ = step;
		else releaseLead_ = step;
	}

	int32_t ReleaseStepFor(uint8_t part) const
	{
		if (part == kPartBass) return releaseBass_;
		if (part == kPartPad) return releasePad_;
		return releaseLead_;
	}

	void SetOut2LaneMask(uint16_t mask) { out2Mask_ = mask; }

	void NoteOn(uint8_t part, uint8_t note, uint8_t vel)
	{
		if (percussive_ || part == kPartDrums || part > kPartPad) return;
		const Instrument &ins = (part == kPartBass) ? bass_
		                      : (part == kPartPad) ? pad_ : lead_;
		if (!ins.data || ins.frames < 2) return;

		Voice *v = Allocate(part);
		if (!v) return;
		v->part = part;
		v->note = note;
		v->lane = 0xFF;
		v->chokeGroup = 0;
		v->loop = ins.loop;
		v->releaseStep = ReleaseStepFor(part);
		v->seq = ++allocSeq_;
		v->Start(ins.data, ins.frames, PitchIncQ12(note, ins.root), VelGain(vel));
	}

	void NoteOff(uint8_t part, uint8_t note)
	{
		if (percussive_) return;
		for (auto &v : voices_)
			if (v.Held() && v.part == part && v.note == note) {
				v.releaseStep = ReleaseStepFor(part);
				v.Release();
			}
	}

	void DrumTrigger(uint32_t lane, uint8_t vel)
	{
		if (!percussive_ || lane >= kNumDrumLanes) return;
		const DrumLane &dl = lanes_[lane];
		if (!dl.data || dl.frames < 2) return;

		// Choke: same group, and any still-sounding copy of this lane
		for (auto &v : voices_)
			if (v.Active() && (v.lane == lane ||
			    (dl.choke != 0 && v.chokeGroup == dl.choke)))
				v.Choke();

		Voice *v = Allocate(kPartDrums);
		if (!v) return;
		v->part = kPartDrums;
		v->note = 0;
		v->lane = (uint8_t)lane;
		v->chokeGroup = dl.choke;
		v->loop = false;
		v->releaseStep = kEnvOne / 480;
		v->seq = ++allocSeq_;
		v->Start(dl.data, dl.frames, kUnityIncQ12, VelGain(vel));
	}

	void ReleaseAll() { for (auto &v : voices_) v.Release(); }
	void KillAll() { for (auto &v : voices_) v.Kill(); }

	// Render one sample. Melodic: outA = lead + pad, outB = bass.
	// Percussive: outA = full kit, outB = out2_lane_mask submix.
	// Sums are in ~int16 range x voice count; caller applies master gain
	// and clamps to the 12-bit DAC range.
	void Render(int32_t &outA, int32_t &outB)
	{
		if (percussive_) {
			int32_t a = 0, b = 0;
			for (auto &v : voices_) {
				if (!v.Active()) continue;
				int32_t s = v.Render();
				a += s;
				if (out2Mask_ & (1u << v.lane)) b += s;
			}
			outA = (a * kGainDrums) >> 12;
			outB = (b * kGainDrums) >> 12;
		} else {
			int32_t lead = 0, bass = 0, pad = 0;
			for (auto &v : voices_) {
				if (!v.Active()) continue;
				int32_t s = v.Render();
				if (v.part == kPartBass) bass += s;
				else if (v.part == kPartPad) pad += s;
				else lead += s;
			}
			outA = ((lead * kGainLead) >> 12) + ((pad * kGainPad) >> 12);
			outB = (bass * kGainBass) >> 12;
		}
	}

	int ActiveVoices() const
	{
		int n = 0;
		for (const auto &v : voices_) n += v.Active() ? 1 : 0;
		return n;
	}

private:
	static int32_t VelGain(uint8_t vel)
	{
		if (vel > 127) vel = 127;
		return (int32_t)vel << 5; // 127 -> 4064 ~= unity in Q12
	}

	int HeldCount(uint8_t part) const
	{
		int n = 0;
		for (const auto &v : voices_) n += (v.Active() && v.part == part) ? 1 : 0;
		return n;
	}

	Voice *Oldest(uint8_t part /* 0xFF = any */)
	{
		Voice *best = nullptr;
		for (auto &v : voices_) {
			if (!v.Active()) continue;
			if (part != 0xFF && v.part != part) continue;
			if (!best || v.seq < best->seq) best = &v;
		}
		return best;
	}

	Voice *Allocate(uint8_t part)
	{
		// Melodic per-part ceilings: steal the oldest voice of the same
		// part rather than starving the other parts.
		if (!percussive_) {
			int limit = (part == kPartBass) ? kMaxBass
			          : (part == kPartPad) ? kMaxPad : kMaxLead;
			if (HeldCount(part) >= limit) return Oldest(part);
		}
		// Free voice first
		for (auto &v : voices_)
			if (!v.Active()) return &v;
		// Then quietest releasing voice
		Voice *best = nullptr;
		for (auto &v : voices_)
			if (v.state == 3 && (!best || v.env < best->env)) best = &v;
		if (best) return best;
		// Then oldest overall
		return Oldest(0xFF);
	}

	Voice voices_[kMaxVoices];
	Instrument lead_, bass_, pad_;
	DrumLane lanes_[kNumDrumLanes];
	int32_t releaseLead_ = kEnvOne / (60 * 48);
	int32_t releaseBass_ = kEnvOne / (120 * 48);
	int32_t releasePad_ = kEnvOne / (240 * 48);
	uint16_t out2Mask_ = 0x0001;
	uint32_t allocSeq_ = 0;
	bool percussive_ = false;
};

} // namespace cowork

#endif
