#include "MidiLink.h"
#include "MidiParser.h"
#include "midi_defs.h"
#include "../SharedState.h"

#include "pico/stdlib.h"
#include "tusb.h"
#include "../usb/usb_midi_host.h"

namespace cowork {

namespace {

uint8_t gMidiDevAddr = 0;
MidiParser gParser;

struct QueueSink : MidiParser::Sink {
	void OnRealtime(uint8_t status) override
	{
		MidiInEvent ev{ time_us_32(), status, 0, 0 };
		gShared.midiIn.Push(ev);
	}
	void OnMessage(uint8_t status, uint8_t d1, uint8_t d2) override
	{
		MidiInEvent ev{ time_us_32(), status, d1, d2 };
		gShared.midiIn.Push(ev);
	}
};
QueueSink gSink;

// Last CV values actually sent, for on-change suppression
uint16_t gCvSent[2] = { 0xFFFF, 0xFFFF };
uint32_t gCvLastSendUs = 0;

} // namespace

bool MidiLink::Connected()
{
	if (gShared.usbHostMode)
		return gMidiDevAddr != 0 && tuh_midi_configured(gMidiDevAddr);
	return tud_midi_mounted();
}

void MidiLink::Send(uint8_t status, uint8_t d1, uint8_t d2, uint8_t len)
{
	uint8_t buf[3] = { status, d1, d2 };
	if (len < 1 || len > 3) return;
	if (gShared.usbHostMode) {
		if (gMidiDevAddr == 0 || !tuh_midi_configured(gMidiDevAddr)) return;
		uint8_t nCables = tuh_midih_get_num_tx_cables(gMidiDevAddr);
		if (nCables < 1) return;
		tuh_midi_stream_write(gMidiDevAddr, 0, buf, len);
	} else {
		if (!tud_midi_mounted()) return;
		tud_midi_stream_write(0, buf, len);
	}
}

void MidiLink::DrainOutQueue()
{
	MidiOutEvent ev;
	bool sent = false;
	while (gShared.midiOut.Pop(ev)) {
		Send(ev.status, ev.d1, ev.d2, ev.len);
		sent = true;
	}
	// Flush per pass so realtime bytes leave in the next USB frame
	if (sent && gShared.usbHostMode && gMidiDevAddr != 0)
		tuh_midi_stream_flush(gMidiDevAddr);
}

void MidiLink::FeedRx(const uint8_t *buf, uint32_t n)
{
	gParser.Feed(buf, n, gSink);
}

void MidiLink::TaskDevice()
{
	DrainOutQueue();

	uint8_t buf[64];
	while (tud_midi_available()) {
		uint32_t n = tud_midi_stream_read(buf, sizeof(buf));
		if (n == 0) break;
		FeedRx(buf, n);
	}
	gShared.peerConnected = tud_midi_mounted() ? 1 : 0;
}

void MidiLink::TaskHost()
{
	DrainOutQueue();
	gShared.peerConnected =
		(gMidiDevAddr != 0 && tuh_midi_configured(gMidiDevAddr)) ? 1 : 0;
}

void MidiLink::ForwardCv(uint8_t fwdBits)
{
	uint32_t now = time_us_32();
	if (now - gCvLastSendUs < kCvFwdMinIntervalUs) return;
	if (!Connected()) return;

	static const uint8_t msbCc[2] = { kCcCv1Msb, kCcCv2Msb };
	static const uint8_t lsbCc[2] = { kCcCv1Lsb, kCcCv2Lsb };
	bool sent = false;

	for (int i = 0; i < 2; i++) {
		if (!(fwdBits & (1 << i))) continue;
		uint16_t v14 = CvTo14(gShared.cvFwd[i]);
		// Suppress sub-LSB(7) wiggle so idle inputs stay quiet
		if ((v14 >> 7) == (gCvSent[i] >> 7) &&
		    ((v14 ^ gCvSent[i]) & 0x7F) < 2 && gCvSent[i] != 0xFFFF)
			continue;
		uint8_t status = (uint8_t)(0xB0 | kLinkChannel);
		Send(status, lsbCc[i], v14 & 0x7F, 3);
		Send(status, msbCc[i], (v14 >> 7) & 0x7F, 3); // MSB last: applies atomically
		gCvSent[i] = v14;
		sent = true;
	}
	if (sent) {
		gCvLastSendUs = now;
		if (gShared.usbHostMode && gMidiDevAddr != 0)
			tuh_midi_stream_flush(gMidiDevAddr);
	}
}

void MidiLink::HostMounted(uint8_t devAddr)
{
	if (gMidiDevAddr == 0) gMidiDevAddr = devAddr;
}

void MidiLink::HostUnmounted()
{
	gMidiDevAddr = 0;
	gParser.Reset();
}

void MidiLink::HostRx(uint8_t devAddr)
{
	if (devAddr != gMidiDevAddr) return;
	uint8_t cable = 0;
	uint8_t buf[64];
	while (true) {
		uint32_t n = tuh_midi_stream_read(devAddr, &cable, buf, sizeof(buf));
		if (n == 0) break;
		FeedRx(buf, n);
	}
}

} // namespace cowork

// ---- rppicomidi usb_midi_host application callbacks (C linkage) ----

extern "C" {

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
                       uint8_t num_cables_rx, uint16_t num_cables_tx)
{
	(void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx;
	cowork::MidiLink::HostMounted(dev_addr);
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	(void)dev_addr; (void)instance;
	cowork::MidiLink::HostUnmounted();
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
	if (num_packets == 0) return;
	cowork::MidiLink::HostRx(dev_addr);
}

void tuh_midi_tx_cb(uint8_t dev_addr)
{
	(void)dev_addr;
}

} // extern "C"
