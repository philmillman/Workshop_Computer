#include "usb_core1.h"
#include "../SharedState.h"
#include "../CoWorkCard.h"
#include "../link/MidiLink.h"
#include "../storage/Banks.h"
#include "../storage/ConfigStore.h"
#include "../storage/UploadManager.h"

#include "pico/stdlib.h"
#include "tusb.h"
#include "bsp/board_api.h"

// Set to 1 if host-mode enumeration fails with tuh_init alone: with
// CFG_TUSB_RHPORT0_MODE = HOST|DEVICE some TinyUSB versions need the full
// tusb_init() to configure the controller for the chosen role
// (releases/33_drumdrum/usb_core1.cpp discussion).
#ifndef COWORK_HOST_USE_TUSB_INIT
#define COWORK_HOST_USE_TUSB_INIT 0
#endif

namespace cowork {

namespace {

ConfigStore gConfigStore;
UploadManager gUpload;
uint32_t gLastApplySeq = 0;

void Pump()
{
	if (gShared.usbHostMode) {
		tuh_task();
		MidiLink::TaskHost();
	} else {
		tud_task();
		MidiLink::TaskDevice();
	}
}

void OnFlashMutated()
{
	RebuildBanks();
}

// Core 0 owns runtime mode/song changes (long-press, Y knob); core 1 is the
// single writer of configStaging, so mirror them here before saving.
void MirrorRuntimeStateIntoConfig()
{
	Config &c = gShared.configStaging;
	bool dirty = false;
	if (c.engine_mode != (uint8_t)gShared.statusMode) {
		c.engine_mode = (uint8_t)gShared.statusMode;
		dirty = true;
	}
	if (c.active_song != (uint8_t)gShared.statusSong) {
		c.active_song = (uint8_t)gShared.statusSong;
		dirty = true;
	}
	if (gShared.configApplySeq != gLastApplySeq) {
		gLastApplySeq = gShared.configApplySeq;
		dirty = true;
	}
	if (dirty) gConfigStore.MarkDirty(time_us_32());
}

} // namespace

void UsbCore1Entry()
{
	// Wait for the USB power circuitry to decide DFP/UFP
	// (midi_device_host example: 150 ms after power-up).
	sleep_us(150000);

	CoWorkCard *card = static_cast<CoWorkCard *>(ComputerCard::ThisPtr());
	ComputerCard::USBPowerState_t ps = card->PowerState();
	gShared.usbHostMode = (ps == ComputerCard::DFP) ? 1 : 0;
	gShared.usbStateKnown = 1;

	board_init();
	if (gShared.usbHostMode) {
#if COWORK_HOST_USE_TUSB_INIT
		tusb_init();
#else
		tuh_init(TUH_OPT_RHPORT);
#endif
	} else {
		tud_init(TUD_OPT_RHPORT);
	}

	gConfigStore.Init(gFlashMap, gShared.configStaging);
	gUpload.Init(gFlashMap, Pump, OnFlashMutated);

	while (true) {
		Pump();
		if (!gShared.usbHostMode)
			gUpload.Service();
		MidiLink::ForwardCv(gShared.configStaging.fwd_enable & 0x03);
		MidiLink::MirrorDiag();
		MirrorRuntimeStateIntoConfig();
		gConfigStore.Service(time_us_32(), gShared.transportRunning != 0,
		                     gShared.configStaging);
	}
}

} // namespace cowork
