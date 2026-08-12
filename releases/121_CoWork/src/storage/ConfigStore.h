// Config persistence (CWCF blob in its own 4 KB sector).
// Load runs on core 0 at boot (single-core, XIP read only).
// Saves run on core 1 only, debounced ~2 s, only while the transport is
// stopped, under the flash-quiesce handshake (Grids-style memcmp first).

#ifndef COWORK_CONFIG_STORE_H
#define COWORK_CONFIG_STORE_H

#include <cstdint>
#include "ConfigFormat.h"
#include "FlashMap.h"

namespace cowork {

class ConfigStore {
public:
	// Boot-time load; falls back to defaults on bad magic/CRC.
	static Config LoadFromFlash(const FlashMap &map);

	void Init(const FlashMap &map, const Config &loaded)
	{
		map_ = map;
		lastSaved_ = loaded;
	}

	void MarkDirty(uint32_t nowUs)
	{
		dirty_ = true;
		dirtyAtUs_ = nowUs;
	}

	// Called from the core-1 loop. `live` is the current staged config;
	// returns true if a save was performed.
	bool Service(uint32_t nowUs, bool transportRunning, const Config &live);

private:
	static constexpr uint32_t kDebounceUs = 2000000;

	FlashMap map_{};
	Config lastSaved_{};
	bool dirty_ = false;
	uint32_t dirtyAtUs_ = 0;
};

} // namespace cowork

#endif
