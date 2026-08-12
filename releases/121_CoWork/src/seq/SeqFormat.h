// Binary song-slot format (CWSQ) — the contract with web/cowork.html.
// See docs/FORMATS.md. Pure header: no pico includes, host-testable.

#ifndef COWORK_SEQ_FORMAT_H
#define COWORK_SEQ_FORMAT_H

#include <cstdint>
#include <cstring>

namespace cowork {

constexpr uint32_t kSeqMagic = 0x51535743u; // "CWSQ"
constexpr uint16_t kSeqVersion = 1;
constexpr uint16_t kPPQN = 96;              // 4x the 24 PPQN MIDI clock

enum EventType : uint8_t {
	kEvNoteOff = 0x00,
	kEvNoteOn  = 0x01,
	kEvTempo   = 0x02,
	kEvEnd     = 0x7F,
};

enum Part : uint8_t {
	kPartLead  = 0,
	kPartBass  = 1,
	kPartDrums = 2,
};

struct __attribute__((packed)) SeqEvent {
	uint32_t tick;   // absolute, 96 PPQN, non-decreasing
	uint8_t  type;   // EventType
	uint8_t  part;   // Part — for TEMPO: uspq bits 23..16
	uint8_t  d1;     // note — for TEMPO: uspq bits 15..8
	uint8_t  d2;     // velocity — for TEMPO: uspq bits 7..0

	uint32_t TempoUsPerQuarter() const
	{
		return ((uint32_t)part << 16) | ((uint32_t)d1 << 8) | d2;
	}
};
static_assert(sizeof(SeqEvent) == 8, "SeqEvent must be 8 bytes");

struct __attribute__((packed)) SeqHeader {
	uint32_t magic;
	uint16_t version;
	uint16_t flags;            // bit0 = loop enabled
	uint16_t ppqn;
	uint8_t  tsig_num;
	uint8_t  tsig_denom_log2;
	uint32_t init_tempo_uspq;
	uint32_t length_ticks;
	uint32_t loop_start_tick;
	uint32_t loop_end_tick;
	uint32_t event_count;      // includes END sentinel
	char     name[24];
	uint32_t events_crc32;
	uint32_t reserved;
};
static_assert(sizeof(SeqHeader) == 64, "SeqHeader must be 64 bytes");

// Validate a slot image in place. Returns nullptr if the slot is empty or
// malformed; otherwise returns the header (events follow immediately).
inline const SeqHeader *ValidateSeqSlot(const uint8_t *slot, uint32_t slotBytes)
{
	if (!slot || slotBytes < sizeof(SeqHeader)) return nullptr;
	SeqHeader h;
	memcpy(&h, slot, sizeof(h)); // avoid unaligned/aliasing pitfalls on XIP
	if (h.magic != kSeqMagic) return nullptr;
	if (h.version != kSeqVersion) return nullptr;
	if (h.ppqn != kPPQN) return nullptr;
	if (h.event_count == 0) return nullptr;
	uint64_t need = sizeof(SeqHeader) + (uint64_t)h.event_count * sizeof(SeqEvent);
	if (need > slotBytes) return nullptr;
	if (h.loop_end_tick > h.length_ticks) return nullptr;
	if (h.loop_start_tick >= h.loop_end_tick && (h.flags & 1)) return nullptr;
	return reinterpret_cast<const SeqHeader *>(slot);
}

inline const SeqEvent *SeqEvents(const SeqHeader *h)
{
	return reinterpret_cast<const SeqEvent *>(
		reinterpret_cast<const uint8_t *>(h) + sizeof(SeqHeader));
}

} // namespace cowork

#endif
