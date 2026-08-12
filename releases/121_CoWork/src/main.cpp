// CoWork — dual-module sample sequencer for the Workshop System Computer.
//
// Boot order matters:
//  1. Voltage + 192 MHz clock (multiple of 48 MHz for clean audio).
//  2. JEDEC flash detect — single-core, before any concurrent XIP activity.
//  3. Config load + initial bank parse (still single-core).
//  4. Launch core 1 (USB role detect + TinyUSB + CDC protocol).
//  5. Run() — 48 kHz audio ISR on core 0; the leader/follower role is
//     latched from the Z switch a few ms in, once the ADC mux has settled.

#include "CoWorkCard.h"
#include "usb/usb_core1.h"
#include "storage/flash_size.h"
#include "storage/ConfigStore.h"
#include "storage/Banks.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

using namespace cowork;

static CoWorkCard gCard;

static void Core1Trampoline()
{
	UsbCore1Entry();
}

int main()
{
	vreg_set_voltage(VREG_VOLTAGE_1_20);
	sleep_ms(1);
	set_sys_clock_khz(192000, true);

	gFlashMap = FlashMap::Compute(cowork_detect_flash_size());

	Config cfg = ConfigStore::LoadFromFlash(gFlashMap);
	gShared.configStaging = cfg;
	gShared.configApplySeq = 1;

	RebuildBanks();

	gCard.EnableNormalisationProbe();
	multicore_launch_core1(Core1Trampoline);
	gCard.Run(); // never returns
}
