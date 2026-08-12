// Flash-quiesce handshake helpers (core 1 side).
//
// Erase/program stalls ALL XIP reads — including core 0's per-sample reads
// of sample data — even though our code runs from RAM (copy_to_ram). So
// before touching flash, core 1 raises flashMutateReq and waits for core 0
// to kill voices, stop reading XIP, and ack via flashQuiesced.

#ifndef COWORK_FLASH_OPS_H
#define COWORK_FLASH_OPS_H

#include <cstdint>

namespace cowork {

class FlashOps {
public:
	// Returns false on timeout (core 0 not running / wedged).
	static bool AcquireQuiesce();
	static void ReleaseQuiesce();
};

} // namespace cowork

#endif
