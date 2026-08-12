// Runtime flash region map, computed once at boot from the JEDEC-detected
// flash size and published to the web UI via the CDC 'I' command.
// Pure header: no pico includes, host-testable. See docs/FORMATS.md §1.

#ifndef COWORK_FLASH_MAP_H
#define COWORK_FLASH_MAP_H

#include <cstdint>

namespace cowork {

constexpr uint32_t kSectorSize = 4096u;
constexpr uint32_t kPageSize = 256u;

#ifndef COWORK_FIRMWARE_RESERVE
#define COWORK_FIRMWARE_RESERVE (256u * 1024u)
#endif

struct FlashMap {
	uint32_t flashBytes;    // detected total
	uint32_t configOff;     // one sector
	uint32_t songsOff;
	uint32_t songSlotBytes; // 64 KB each
	uint32_t songSlots;     // 8 (>=8MB) or 4
	uint32_t bankOff;       // sample directory sector
	uint32_t bankDataOff;   // first sample data byte
	uint32_t bankEnd;       // == flashBytes

	static FlashMap Compute(uint32_t detectedFlashBytes)
	{
		FlashMap m;
		m.flashBytes = detectedFlashBytes;
		m.configOff = COWORK_FIRMWARE_RESERVE;
		m.songsOff = m.configOff + kSectorSize;
		m.songSlotBytes = 64u * 1024u;
		m.songSlots = (detectedFlashBytes >= 8u * 1024u * 1024u) ? 8u : 4u;
		m.bankOff = m.songsOff + m.songSlots * m.songSlotBytes;
		m.bankDataOff = m.bankOff + kSectorSize;
		m.bankEnd = detectedFlashBytes;
		return m;
	}

	uint32_t SongSlotOff(uint32_t slot) const { return songsOff + slot * songSlotBytes; }
	uint32_t BankDataBytes() const { return bankEnd - bankDataOff; }

	// True if [off, off+len) lies entirely within the web-writable regions
	// (song slots or sample bank, directory sector included).
	bool InWritableRegion(uint32_t off, uint32_t len) const
	{
		if (len == 0 || off + len < off) return false; // overflow
		uint32_t end = off + len;
		bool inSongs = off >= songsOff && end <= bankOff;
		bool inBank = off >= bankOff && end <= bankEnd;
		return inSongs || inBank;
	}
};

} // namespace cowork

#endif
