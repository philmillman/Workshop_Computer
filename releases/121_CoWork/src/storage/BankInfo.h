// Parsed, RAM-resident view of the flash sample bank + song directory.
// Core 1 rebuilds this after every flash mutation (double-buffered in
// SharedState); core 0 only ever reads a fully-built copy.
//
// Pure header: parsing takes plain pointers, so sim tests can feed RAM
// images instead of XIP flash.

#ifndef COWORK_BANK_INFO_H
#define COWORK_BANK_INFO_H

#include <cstdint>
#include <cstring>
#include "FlashMap.h"
#include "SampleBankFormat.h"
#include "../seq/SeqFormat.h"

namespace cowork {

struct BankInfo {
	struct Melodic {
		const int16_t *data = nullptr;
		uint32_t frames = 0;
		uint8_t root = 60;
		bool loop = false;
	};
	struct Lane {
		const int16_t *data = nullptr;
		uint32_t frames = 0;
		uint8_t choke = 0;
	};
	struct Song {
		const uint8_t *slot = nullptr;
		uint32_t bytes = 0;
		bool valid = false;
	};

	Melodic lead, bass;
	Lane lane[kNumDrumLanes];
	uint8_t noteToLane[128]; // 0xFF = unmapped
	Song song[8];
	uint32_t songSlots = 0;
	bool bankValid = false;

	void Clear()
	{
		lead = Melodic{};
		bass = Melodic{};
		for (auto &l : lane) l = Lane{};
		memset(noteToLane, 0xFF, sizeof(noteToLane));
		for (auto &s : song) s = Song{};
		songSlots = 0;
		bankValid = false;
	}
};

// flashBase = XIP-mapped pointer to flash offset 0 (or a RAM image in tests).
inline void BuildBankInfo(BankInfo &out, const uint8_t *flashBase, const FlashMap &map)
{
	out.Clear();
	out.songSlots = map.songSlots;

	// Song slots: validate headers in place
	for (uint32_t i = 0; i < map.songSlots && i < 8; i++) {
		const uint8_t *slot = flashBase + map.SongSlotOff(i);
		out.song[i].slot = slot;
		out.song[i].bytes = map.songSlotBytes;
		out.song[i].valid = ValidateSeqSlot(slot, map.songSlotBytes) != nullptr;
	}

	// Sample directory
	const uint8_t *dir = flashBase + map.bankOff;
	uint32_t bankBytes = map.bankEnd - map.bankOff;
	if (!ValidateBankHeader(dir, bankBytes)) return;
	out.bankValid = true;

	for (uint32_t i = 0; i < kBankSlots; i++) {
		SampleSlot s;
		memcpy(&s, dir + sizeof(BankHeader) + i * sizeof(SampleSlot), sizeof(s));
		if (!SlotIsPopulated(s, bankBytes)) continue;

		const int16_t *pcm = reinterpret_cast<const int16_t *>(flashBase + map.bankOff + s.offset);
		if (i == kSlotLead || i == kSlotBass) {
			BankInfo::Melodic &m = (i == kSlotLead) ? out.lead : out.bass;
			m.data = pcm;
			m.frames = s.length_frames;
			m.root = (s.root_note <= 127) ? s.root_note : 60;
			m.loop = (s.flags & 1) != 0;
		} else {
			uint32_t laneIdx = i - kSlotDrum0;
			out.lane[laneIdx].data = pcm;
			out.lane[laneIdx].frames = s.length_frames;
			out.lane[laneIdx].choke = s.choke_group;
			if (s.assign_note <= 127)
				out.noteToLane[s.assign_note] = (uint8_t)laneIdx;
		}
	}
}

} // namespace cowork

#endif
