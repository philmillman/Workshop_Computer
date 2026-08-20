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
		if (!gShared.midiIn.Push(ev))
			gShared.diagMidiInDrops = gShared.diagMidiInDrops + 1;
	}
	void OnMessage(uint8_t status, uint8_t d1, uint8_t d2) override
	{
		MidiInEvent ev{ time_us_32(), status, d1, d2 };
		if (!gShared.midiIn.Push(ev))
			gShared.diagMidiInDrops = gShared.diagMidiInDrops + 1;
	}
};
QueueSink gSink;

// Last CV values actually sent, for on-change suppression
uint16_t gCvSent[2] = { 0xFFFF, 0xFFFF };
uint32_t gCvSentAtUs[2] = { 0, 0 };

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

	// A stream_write can accept only part of a message when the TX FIFO
	// is full (the ComputerCard examples warn about exactly this). A
	// half-written message leaves the USB-MIDI packetizer mid-message and
	// later messages — including 0xF8 clocks — get eaten, which starved
	// the follower's clock under CV-forwarding load. Pump USB and finish
	// the message; give up whole only if the host stalls outright.
	uint32_t sent = 0;
	uint32_t t0 = time_us_32();
	if (gShared.usbHostMode) {
		if (gMidiDevAddr == 0 || !tuh_midi_configured(gMidiDevAddr)) return;
		if (tuh_midih_get_num_tx_cables(gMidiDevAddr) < 1) return;
		while (sent < len) {
			sent += tuh_midi_stream_write(gMidiDevAddr, 0, buf + sent, len - sent);
			if (sent < len) {
				if (time_us_32() - t0 > 2000) return;
				tuh_midi_stream_flush(gMidiDevAddr);
				tuh_task();
			}
		}
	} else {
		if (!tud_midi_mounted()) return;
		while (sent < len) {
			sent += tud_midi_stream_write(0, buf + sent, len - sent);
			if (sent < len) {
				if (time_us_32() - t0 > 2000) return;
				tud_task();
			}
		}
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
	if (!Connected()) return;
	uint32_t now = time_us_32();

	static const uint8_t bendChan[2] = { kCvBendChannel1, kCvBendChannel2 };
	bool sent = false;

	for (int i = 0; i < 2; i++) {
		if (!(fwdBits & (1 << i))) continue;
		if (now - gCvSentAtUs[i] < kCvFwdMinIntervalUs) continue;
		uint16_t v14 = CvTo14(gShared.cvFwd[i]);
		// Suppress small wiggle (ADC noise) so idle inputs stay quiet —
		// but refresh periodically so a static CV never reads as stale.
		int32_t diff = (int32_t)v14 - (int32_t)gCvSent[i];
		bool changed = gCvSent[i] == 0xFFFF || diff <= -8 || diff >= 8;
		if (!changed && now - gCvSentAtUs[i] < kCvKeepAliveUs)
			continue;
		// Pitch bend: one atomic 3-byte message per value
		Send((uint8_t)(0xE0 | bendChan[i]), v14 & 0x7F, (v14 >> 7) & 0x7F, 3);
		gCvSent[i] = v14;
		gCvSentAtUs[i] = now;
		sent = true;
	}
	if (sent && gShared.usbHostMode && gMidiDevAddr != 0)
		tuh_midi_stream_flush(gMidiDevAddr);
}

void MidiLink::MirrorDiag()
{
	static uint32_t lastUs = 0;
	uint32_t now = time_us_32();
	if (now - lastUs < kDiagMirrorIntervalUs) return;
	if (!Connected()) return;
	lastUs = now;

	const uint32_t v[7] = {
		gShared.diagResyncs, gShared.diagFreewheels, gShared.diagStopsRx,
		gShared.diagStartsRx, gShared.diagMaxGapMs / 100,
		gShared.diagMidiInDrops, gShared.diagOverrun,
	};
	for (int i = 0; i < 7; i++)
		Send((uint8_t)(0xB0 | kLinkChannel), (uint8_t)(kCcDiagBase + i),
		     (uint8_t)(v[i] > 127 ? 127 : v[i]), 3);
	if (gShared.usbHostMode && gMidiDevAddr != 0)
		tuh_midi_stream_flush(gMidiDevAddr);
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
