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

// CV/pulse forwarding channel (0-based): MIDI channel 16
constexpr uint8_t kLinkChannel = 15;

constexpr uint8_t kCcCv1Msb = 20;
constexpr uint8_t kCcCv2Msb = 21;
constexpr uint8_t kCcCv1Lsb = 52;
constexpr uint8_t kCcCv2Lsb = 53;

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

// Minimum interval between forwarded CV updates (200 Hz)
constexpr uint32_t kCvFwdMinIntervalUs = 5000;

// Remote values expire after this long without an update
constexpr uint32_t kRemoteStaleUs = 500000;

} // namespace cowork

#endif
