// Runtime detection of the program card's flash capacity via the JEDEC ID
// (command 0x9F). Adapted from releases/11_goldfish/flash_size.h.
//
// IMPORTANT: call cowork_detect_flash_size() once, early in init, while
// still single-core and before any concurrent XIP flash activity —
// flash_do_cmd() momentarily takes over the SSI.

#ifndef COWORK_FLASH_SIZE_H
#define COWORK_FLASH_SIZE_H

#include <stdint.h>
#include "hardware/flash.h"

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t cowork_detect_flash_size(void)
{
	uint8_t tx[4] = { 0x9fu, 0u, 0u, 0u };
	uint8_t rx[4] = { 0u, 0u, 0u, 0u };

	flash_do_cmd(tx, rx, 4);

	// Some helpers report {mfr, type, capacity}, others shift by the
	// command byte; accept a sane log2 code from either position.
	uint8_t capacity_code = rx[3];
	if (capacity_code < 21u || capacity_code > 25u)
		capacity_code = rx[2];

	// 0x15 = 2 MB (2^21) .. 0x19 = 32 MB. Fall back to 2 MB when unsure —
	// a too-small map is always safe.
	if (capacity_code >= 21u && capacity_code <= 25u)
		return 1u << capacity_code;

	return 2u * 1024u * 1024u;
}

#ifdef __cplusplus
}
#endif

#endif
