#include "ConfigStore.h"
#include "Crc32.h"
#include "FlashOps.h"

#include <cstring>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

namespace cowork {

Config ConfigStore::LoadFromFlash(const FlashMap &map)
{
	Config c;
	const uint8_t *src = (const uint8_t *)(XIP_BASE + map.configOff);
	memcpy(&c, src, sizeof(c));

	bool ok = c.magic == kConfigMagic && c.version == kConfigVersion &&
	          c.crc32 == Crc32::Compute((const uint8_t *)&c, kConfigBytes - 4);
	if (!ok)
		ConfigSetDefaults(c);
	ConfigSanitize(c, map.songSlots);
	return c;
}

bool ConfigStore::Service(uint32_t nowUs, bool transportRunning, const Config &live)
{
	if (!dirty_ || transportRunning) return false;
	if (nowUs - dirtyAtUs_ < kDebounceUs) return false;

	Config c = live;
	c.crc32 = Crc32::Compute((const uint8_t *)&c, kConfigBytes - 4);

	if (memcmp(&c, &lastSaved_, sizeof(c)) == 0) {
		dirty_ = false;
		return false;
	}

	// One sector: erase + program a full page under quiesce.
	uint8_t page[kPageSize];
	memset(page, 0xFF, sizeof(page));
	memcpy(page, &c, sizeof(c));

	if (!FlashOps::AcquireQuiesce()) return false; // retry next pass
	uint32_t ints = save_and_disable_interrupts();
	flash_range_erase(map_.configOff, kSectorSize);
	flash_range_program(map_.configOff, page, sizeof(page));
	restore_interrupts(ints);
	FlashOps::ReleaseQuiesce();

	lastSaved_ = c;
	dirty_ = false;
	return true;
}

} // namespace cowork
