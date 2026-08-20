// MIDI link protocol constants — see docs/FORMATS.md §6.

#ifndef COWORK_MIDI_DEFS_H
#define COWORK_MIDI_DEFS_H

#include <cstdint>

namespace cowork {

// Realtime
constexpr uint8_t kMidiClock = 0xF8;
constexpr uint8_t kMidiStart = 0xFA;
constexpr uint8_t kMidiContinue = 0xFB;
constexpr uint8_t kMidiStop = 0xFC;

// Pulse forwarding channel (0-based): MIDI channel 16
constexpr uint8_t kLinkChannel = 15;

// CV forwarding: pitch bend on channels 15 (CV1) and 16 (CV2). Bend is a
// single 3-byte message carrying all 14 bits ATOMICALLY — the earlier
// CC MSB/LSB pair kept pairing state across two messages, and with both
// channels streaming, one dropped message desynced every later value.
constexpr uint8_t kCvBendChannel1 = 14; // MIDI channel 15
constexpr uint8_t kCvBendChannel2 = 15; // MIDI channel 16

constexpr uint8_t kNotePulse1 = 60;
constexpr uint8_t kNotePulse2 = 62;

// 12-bit signed CV (+/-2047) <-> 14-bit unsigned wire value
inline uint16_t CvTo14(int32_t cv)
{
	int32_t v = cv + 2048;
	if (v < 0) v = 0;
	if (v > 4095) v = 4095;
	return (uint16_t)(v << 2);
}

inline int32_t CvFrom14(uint16_t v14)
{
	return (int32_t)(v14 >> 2) - 2048;
}

// Minimum interval between forwarded CV updates, per channel (200 Hz)
constexpr uint32_t kCvFwdMinIntervalUs = 5000;

// Keep-alive: resend an unchanged CV at this interval so a static CV
// never trips the receiver's staleness fallback
constexpr uint32_t kCvKeepAliveUs = 250000;

// Remote values expire after this long without an update
constexpr uint32_t kRemoteStaleUs = 500000;

// Diagnostics mirror: each module broadcasts its counters once per second
// as CCs on the link channel — the module link occupies the ONLY USB port,
// so a linked module can't be inspected live. After a run, plug either
// module into the computer (no power cycle: counters live in RAM, the rack
// powers the module) and it reports both sides. Values cap at 127; the
// gap counter is sent as gap/100 ms.
constexpr uint8_t kCcDiagBase = 102; // ..108: rs, fw, stp, str, gap, drp, ovr
constexpr uint32_t kDiagMirrorIntervalUs = 1000000;

} // namespace cowork

#endif
