// Double-buffered BankInfo: core 1 rebuilds the inactive copy after any
// flash mutation and bumps gShared.bankSeq; core 0 adopts the new copy at
// the next audio sample. Index = bankSeq & 1.

#ifndef COWORK_BANKS_H
#define COWORK_BANKS_H

#include "BankInfo.h"
#include "FlashMap.h"

namespace cowork {

extern BankInfo gBankInfo[2];
extern FlashMap gFlashMap;

// Parse flash directories into the inactive buffer and publish it.
// Called by core 0 once at boot (single-core) and by core 1 after writes.
void RebuildBanks();

inline const BankInfo &ActiveBank(uint32_t seq) { return gBankInfo[seq & 1]; }

} // namespace cowork

#endif
