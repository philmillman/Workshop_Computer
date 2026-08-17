// Sample bank directory format (CWSB) — the contract with web/cowork.html.
// See docs/FORMATS.md §3. Pure header: no pico includes, host-testable.

#ifndef COWORK_SAMPLE_BANK_FORMAT_H
#define COWORK_SAMPLE_BANK_FORMAT_H

#include <cstdint>
#include <cstring>

namespace cowork {

constexpr uint32_t kBankMagic = 0x42535743u; // "CWSB"
// Version 2 = the 19-slot layout (pad at 2, drums at 3..18). Version 1 was
// the 18-slot pre-pad layout; the web manager migrates v1 directories, the
// firmware rejects them (an unversioned reinterpretation scrambles slots).
constexpr uint16_t kBankVersion = 2;
constexpr uint16_t kBankSlots = 19; // 0 lead, 1 bass, 2 pad, 3..18 drums
constexpr uint32_t kBankSampleRate = 24000;
constexpr uint32_t kNumDrumLanes = 16;

constexpr uint32_t kSlotLead = 0;
constexpr uint32_t kSlotBass = 1;
constexpr uint32_t kSlotPad = 2;
constexpr uint32_t kSlotDrum0 = 3;

struct __attribute__((packed)) SampleSlot {
	char     name[12];
	uint32_t offset;        // bytes from bank base; >= 0x1000, 4K aligned; 0xFFFFFFFF = empty
	uint32_t length_frames; // int16 mono frames; 0 = empty
	uint32_t sample_rate;   // must be 24000 in v1
	uint8_t  root_note;     // melodic; 0xFF for drums
	uint8_t  assign_note;   // drums; 0xFF for melodic
	uint8_t  flags;         // bit0 = loop
	uint8_t  choke_group;   // 0 = none, 1..4
	uint32_t data_crc32;
};
static_assert(sizeof(SampleSlot) == 32, "SampleSlot must be 32 bytes");

struct __attribute__((packed)) BankHeader {
	uint32_t magic;
	uint16_t version;
	uint16_t slot_count;
	uint32_t sample_rate;
	uint32_t data_bytes_used;
	uint32_t dir_crc32;     // over the slot_count * 32 entry bytes
	uint8_t  reserved[12];
};
static_assert(sizeof(BankHeader) == 32, "BankHeader must be 32 bytes");

// Small helper so this header stays free of FlashMap.h
constexpr uint32_t kSectorConst() { return 4096u; }

// Validate a directory sector image. bankBytes = total bank size including
// the directory sector. Returns false if empty/malformed. Slot entries are
// individually re-checked by the consumer (a bad slot is skipped, not fatal).
inline bool ValidateBankHeader(const uint8_t *dir, uint32_t bankBytes)
{
	if (!dir || bankBytes < kSectorConst()) return false;
	BankHeader h;
	memcpy(&h, dir, sizeof(h));
	if (h.magic != kBankMagic) return false;
	if (h.version != kBankVersion) return false;
	if (h.slot_count != kBankSlots) return false;
	if (h.sample_rate != kBankSampleRate) return false;
	return true;
}

inline bool SlotIsPopulated(const SampleSlot &s, uint32_t bankBytes)
{
	if (s.offset == 0xFFFFFFFFu || s.length_frames == 0) return false;
	if (s.sample_rate != kBankSampleRate) return false;
	if (s.offset < kSectorConst() || (s.offset & (kSectorConst() - 1)) != 0) return false;
	uint64_t end = (uint64_t)s.offset + (uint64_t)s.length_frames * 2u;
	if (end > bankBytes) return false;
	return true;
}

} // namespace cowork

#endif
