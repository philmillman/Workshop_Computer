#include "Banks.h"
#include "../SharedState.h"

#include "hardware/regs/addressmap.h"

namespace cowork {

BankInfo gBankInfo[2];
FlashMap gFlashMap;
Shared gShared;

void RebuildBanks()
{
	uint32_t next = gShared.bankSeq + 1;
	BuildBankInfo(gBankInfo[next & 1], (const uint8_t *)XIP_BASE, gFlashMap);
	gShared.bankSeq = next;
}

} // namespace cowork
