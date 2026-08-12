#include "FlashOps.h"
#include "../SharedState.h"

#include "pico/stdlib.h"

namespace cowork {

bool FlashOps::AcquireQuiesce()
{
	gShared.flashMutateReq = 1;
	// Core 0 acks within one audio sample (~21 us) when Run() is active.
	// A generous timeout covers boot-order races.
	uint32_t start = time_us_32();
	while (!gShared.flashQuiesced) {
		if (time_us_32() - start > 100000) {
			gShared.flashMutateReq = 0;
			return false;
		}
		tight_loop_contents();
	}
	return true;
}

void FlashOps::ReleaseQuiesce()
{
	gShared.flashMutateReq = 0;
	// Core 0 clears flashQuiesced on its next sample; no need to wait.
}

} // namespace cowork
