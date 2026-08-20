// Cross-core shared state. Core 0 owns the audio/sequencer; core 1 owns
// USB and all flash writes. Everything here is lock-free: SPSC rings with
// release/acquire barriers, latest-value mailboxes, and flag handshakes.
//
// Deliberately NO multicore_lockout anywhere — the flash-quiesce handshake
// below replaces it (see releases/82_Computer_Grids/context.md for the
// lockout/FIFO deadlock class this avoids).

#ifndef COWORK_SHARED_STATE_H
#define COWORK_SHARED_STATE_H

#include <cstdint>
#include "hardware/sync.h"
#include "storage/ConfigFormat.h"

namespace cowork {

template <typename T, uint32_t N>
struct SpscQueue {
	static_assert((N & (N - 1)) == 0, "N must be a power of two");

	bool Push(const T &v) // single producer
	{
		uint32_t h = head, t = tail;
		if (h - t >= N) return false;
		buf[h & (N - 1)] = v;
		__dmb();
		head = h + 1;
		return true;
	}

	bool Pop(T &v) // single consumer
	{
		uint32_t t = tail;
		if (t == head) return false;
		__dmb();
		v = buf[t & (N - 1)];
		__dmb();
		tail = t + 1;
		return true;
	}

	T buf[N];
	volatile uint32_t head = 0;
	volatile uint32_t tail = 0;
};

// Core 0 -> core 1: outgoing MIDI (clock, transport, forwarded pulses)
struct MidiOutEvent {
	uint8_t status;
	uint8_t d1;
	uint8_t d2;
	uint8_t len; // 1..3
};

// Core 1 -> core 0: incoming MIDI, timestamped at USB receipt
struct MidiInEvent {
	uint32_t tUs;
	uint8_t status;
	uint8_t d1;
	uint8_t d2;
};

struct Shared {
	SpscQueue<MidiOutEvent, 128> midiOut;
	SpscQueue<MidiInEvent, 128> midiIn;

	// USB role (set once by core 1 after the power-state settle)
	volatile uint32_t usbHostMode = 0;   // 1 = USB host
	volatile uint32_t usbStateKnown = 0; // 1 once decided
	volatile uint32_t peerConnected = 0; // MIDI link active

	// Flash quiesce handshake (core 1 requests, core 0 acks). While
	// quiesced, core 0 must not read sample/sequence data from XIP.
	volatile uint32_t flashMutateReq = 0;
	volatile uint32_t flashQuiesced = 0;

	// Bank info double-buffer: core 1 parses flash directories into
	// bankInfo[!(bankSeq & 1)] and bumps bankSeq; core 0 adopts on change.
	volatile uint32_t bankSeq = 0;

	// Transport (core 0 owns `running`; core 1 requests changes)
	volatile uint32_t transportRunning = 0;
	volatile uint32_t transportStopReq = 0;
	volatile uint32_t transportPlayReq = 0;

	// Config: core 1 stages a new blob (S command / debounced save reads
	// the live one back). configApplySeq bumps on each staged blob.
	Config configStaging;
	volatile uint32_t configApplySeq = 0;
	volatile uint32_t configApplied = 0;

	// Live status snapshot written by core 0 (for I / T? JSON)
	volatile uint32_t statusBpmX10 = 1200;
	volatile uint32_t statusTick = 0;
	volatile uint32_t statusRole = 0; // 0 leader, 1 follower
	volatile uint32_t statusMode = 0; // 0 melodic, 1 percussive
	volatile uint32_t statusSong = 0;
	volatile uint32_t statusConflict = 0; // dual-leader detected

	// CV forwarding mailboxes (core 0 writes latest, core 1 rate-limits)
	volatile int32_t cvFwd[2] = {0, 0};

	// Diagnostics (monotonic counters, exposed in the T? JSON) — turn
	// "the follower died" field reports into data.
	volatile uint32_t diagResyncs = 0;     // DLL hard resyncs
	volatile uint32_t diagFreewheels = 0;  // clock-loss freewheel entries
	volatile uint32_t diagStopsRx = 0;     // 0xFC received
	volatile uint32_t diagStartsRx = 0;    // 0xFA received
	volatile uint32_t diagMaxGapMs = 0;    // worst clock gap seen
	volatile uint32_t diagMidiInDrops = 0; // midiIn queue overflow (core 1)
	volatile uint32_t diagOverrun = 0;     // audio budget overrun latch

	// Peer's mirrored diagnostics (received over the link, 7-bit capped;
	// gap arrives as gap/100 ms). Index order matches kCcDiagBase docs.
	volatile uint32_t peerDiag[7] = {0, 0, 0, 0, 0, 0, 0};
	volatile uint32_t peerDiagSeen = 0;

	// Remote (peer-forwarded) input values, written by core 0 from midiIn
	// consumption — kept here only for status/debug visibility.
	volatile uint32_t uploadActive = 0; // CDC transfer in progress (LED)
};

extern Shared gShared;

} // namespace cowork

#endif
