// CDC protocol handler ("COWORK1") — see docs/FORMATS.md §5.
// Runs on core 1 only. Streams uploads into flash with just-in-time sector
// erase and 256-byte page programming (stretchcore pattern), CRC32-verified
// both over the wire and against read-back.

#ifndef COWORK_UPLOAD_MANAGER_H
#define COWORK_UPLOAD_MANAGER_H

#include <cstdint>
#include "FlashMap.h"

namespace cowork {

class UploadManager {
public:
	// pump: keeps USB alive while blocking (tud_task + MIDI drain).
	// onMutated: rebuild BankInfo after any successful flash change.
	using PumpFn = void (*)();
	using MutatedFn = void (*)();

	void Init(const FlashMap &map, PumpFn pump, MutatedFn onMutated)
	{
		map_ = map;
		pump_ = pump;
		onMutated_ = onMutated;
	}

	// Call once per core-1 loop pass; consumes at most one command.
	void Service();

private:
	static constexpr uint32_t kInterByteTimeoutUs = 2000000;
	static constexpr uint32_t kChunk = 1024;

	bool ReadExact(uint8_t *dst, uint32_t n);
	void WriteAll(const uint8_t *src, uint32_t n);
	void WriteStr(const char *s);
	void WriteU32(uint32_t v);

	void CmdInfo();
	void CmdRead();
	void CmdWrite();
	void CmdErase();
	void CmdConfigGet();
	void CmdConfigSet();
	void CmdTransport();

	FlashMap map_{};
	PumpFn pump_ = nullptr;
	MutatedFn onMutated_ = nullptr;
};

} // namespace cowork

#endif
