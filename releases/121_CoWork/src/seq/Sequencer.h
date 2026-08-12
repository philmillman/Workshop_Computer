// Event-stream sequencer: plays a CWSQ slot straight from XIP flash with an
// integer cursor. Phase is Q16 fractional ticks at 96 PPQN, advanced once
// per audio sample. Pure header, host-testable.

#ifndef COWORK_SEQUENCER_H
#define COWORK_SEQUENCER_H

#include <cstdint>
#include "SeqFormat.h"

namespace cowork {

class Sequencer {
public:
	struct Listener {
		virtual void OnNoteOn(uint8_t part, uint8_t note, uint8_t vel) = 0;
		virtual void OnNoteOff(uint8_t part, uint8_t note) = 0;
		virtual void OnTempo(uint32_t uspq) = 0;
		virtual void OnLoopWrap() = 0;
	};

	// Advance() result bits
	static constexpr uint32_t kAdvTick = 1;    // crossed an integer tick
	static constexpr uint32_t kAdvWrapped = 2; // looped back
	static constexpr uint32_t kAdvEnded = 4;   // non-looping song finished

	bool Attach(const uint8_t *slot, uint32_t slotBytes)
	{
		header_ = ValidateSeqSlot(slot, slotBytes);
		events_ = header_ ? SeqEvents(header_) : nullptr;
		Reset();
		return header_ != nullptr;
	}

	void Detach() { header_ = nullptr; events_ = nullptr; Reset(); }
	bool Loaded() const { return header_ != nullptr; }
	const SeqHeader *Header() const { return header_; }

	void Reset()
	{
		phaseQ16_ = 0;
		cursor_ = 0;
		ended_ = false;
	}

	// Jump to an absolute tick (follower hard resync). Dispatches nothing;
	// the caller should release hanging notes first.
	void SeekTick(uint64_t tickQ16)
	{
		phaseQ16_ = tickQ16;
		uint32_t tick = (uint32_t)(tickQ16 >> 16);
		if (!header_) return;
		if (header_->flags & 1) {
			// Fold into the loop region
			uint32_t start = header_->loop_start_tick;
			uint32_t end = header_->loop_end_tick;
			if (tick >= end && end > start) {
				uint32_t span = end - start;
				tick = start + (tick - start) % span;
				phaseQ16_ = ((uint64_t)tick << 16) | (tickQ16 & 0xFFFF);
			}
		}
		cursor_ = LowerBound(tick);
		ended_ = false;
	}

	void SetTickIncQ16(uint32_t inc) { tickIncQ16_ = inc; }
	uint32_t TickIncQ16() const { return tickIncQ16_; }

	uint32_t Tick() const { return (uint32_t)(phaseQ16_ >> 16); }
	uint64_t PhaseQ16() const { return phaseQ16_; }

	// Advance one audio sample; dispatch due events. Tick increments are
	// always < 1 per sample (240 BPM -> 0.008 ticks/sample), so at most one
	// integer tick is crossed per call.
	uint32_t Advance(Listener &l)
	{
		if (!header_ || ended_) return 0;

		uint32_t before = Tick();
		phaseQ16_ += tickIncQ16_;
		uint32_t now = Tick();
		uint32_t result = (now != before) ? kAdvTick : 0;

		while (cursor_ < header_->event_count && events_[cursor_].tick <= now) {
			const SeqEvent &ev = events_[cursor_];
			cursor_++;
			switch (ev.type) {
			case kEvNoteOn: l.OnNoteOn(ev.part, ev.d1, ev.d2); break;
			case kEvNoteOff: l.OnNoteOff(ev.part, ev.d1); break;
			case kEvTempo: l.OnTempo(ev.TempoUsPerQuarter()); break;
			default: break; // END sentinel and unknown types
			}
		}

		if (header_->flags & 1) {
			uint32_t end = header_->loop_end_tick;
			if (now >= end) {
				l.OnLoopWrap();
				uint32_t start = header_->loop_start_tick;
				phaseQ16_ -= ((uint64_t)(end - start)) << 16;
				cursor_ = LowerBound(Tick());
				result |= kAdvWrapped;
			}
		} else if (now >= header_->length_ticks) {
			ended_ = true;
			result |= kAdvEnded;
		}
		return result;
	}

	// Q16 tick increment per 48 kHz sample for a given tempo.
	// ticks/sample = 96 / (uspq_per_tick... ) = 1e6 * 96 / (uspq * 48000)
	static uint32_t TickIncForTempo(uint32_t uspq)
	{
		if (uspq < 100000) uspq = 100000;    // clamp > 600 BPM
		if (uspq > 6000000) uspq = 6000000;  // clamp < 10 BPM
		// (96 * 65536 * 1e6) / (uspq * 48000) = 96*65536*125 / (uspq*6)
		return (uint32_t)(((uint64_t)kPPQN * 65536u * 125u) / ((uint64_t)uspq * 6u));
	}

	static uint32_t TickIncForBpmX10(uint32_t bpmX10)
	{
		// uspq = 60e6 / bpm = 6e8 / bpmX10
		if (bpmX10 < 100) bpmX10 = 100;
		return TickIncForTempo((uint32_t)(600000000ull / bpmX10));
	}

private:
	// First event index with tick >= t
	uint32_t LowerBound(uint32_t t) const
	{
		uint32_t lo = 0, hi = header_->event_count;
		while (lo < hi) {
			uint32_t mid = (lo + hi) / 2;
			if (events_[mid].tick < t) lo = mid + 1;
			else hi = mid;
		}
		return lo;
	}

	const SeqHeader *header_ = nullptr;
	const SeqEvent *events_ = nullptr;
	uint64_t phaseQ16_ = 0;
	uint32_t cursor_ = 0;
	uint32_t tickIncQ16_ = 0;
	bool ended_ = false;
};

} // namespace cowork

#endif
