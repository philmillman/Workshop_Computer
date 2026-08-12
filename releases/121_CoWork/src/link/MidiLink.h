// MIDI over the USB link, both electrical roles behind one interface.
// Runs entirely on core 1: drains the core-0 midiOut queue (clock,
// transport, forwarded pulses), parses incoming bytes into timestamped
// midiIn events, and rate-limits CV forwarding.

#ifndef COWORK_MIDI_LINK_H
#define COWORK_MIDI_LINK_H

#include <cstdint>

namespace cowork {

class MidiLink {
public:
	static void TaskDevice(); // call every core-1 pass in device mode
	static void TaskHost();   // call every core-1 pass in host mode

	// Direct send (CV forwarder). len 1..3.
	static void Send(uint8_t status, uint8_t d1, uint8_t d2, uint8_t len);

	// CV forwarding: reads gShared.cvFwd, sends 14-bit CC pairs, <= 200 Hz,
	// on change only. fwdBits = live config fwd_enable.
	static void ForwardCv(uint8_t fwdBits);

	static bool Connected();

	// Glue for the vendored usb_midi_host callbacks
	static void HostMounted(uint8_t devAddr);
	static void HostUnmounted();
	static void HostRx(uint8_t devAddr);

private:
	static void DrainOutQueue();
	static void FeedRx(const uint8_t *buf, uint32_t n);
};

} // namespace cowork

#endif
