// Persisted config blob (CWCF) — the contract with web/cowork.html.
// See docs/FORMATS.md §4. Pure header: no pico includes, host-testable.

#ifndef COWORK_CONFIG_FORMAT_H
#define COWORK_CONFIG_FORMAT_H

#include <cstdint>
#include <cstring>

namespace cowork {

constexpr uint32_t kConfigMagic = 0x46435743u; // "CWCF"
constexpr uint16_t kConfigVersion = 1;
constexpr uint32_t kConfigBytes = 128;

// out_src_bits / fwd_enable bit positions
constexpr uint8_t kOutCV1 = 1 << 0;
constexpr uint8_t kOutCV2 = 1 << 1;
constexpr uint8_t kOutPulse1 = 1 << 2;
constexpr uint8_t kOutPulse2 = 1 << 3;

struct __attribute__((packed)) Config {
	uint32_t magic;
	uint16_t version;
	uint8_t  engine_mode;      // 0 melodic, 1 percussive
	uint8_t  active_song;
	uint16_t tempo_bpm_x10;    // 400..2400
	uint8_t  swing;            // 50..75
	uint8_t  out_src_bits;     // 0 = local, 1 = remote per output
	uint8_t  fwd_enable;       // forward CV/pulse inputs to peer
	uint8_t  pulse1_lane;      // drum lane on PulseOut1 (percussive)
	uint8_t  pulse2_lane;
	uint8_t  cv2_lane;         // lane velocity on CVOut2 (percussive)
	uint16_t out2_lane_mask;   // AudioOut2 submix (percussive)
	uint8_t  release_lead;     // ms/4
	uint8_t  release_bass;     // ms/4
	uint8_t  master_vol;       // 0..255
	uint8_t  release_pad;      // ms/4
	uint8_t  midi_channel_to_part[16]; // 0 lead/1 bass/2 drums/3 pad or 0xFF ignore
	uint8_t  reserved[86];
	uint32_t crc32;            // over bytes 0..123
};
static_assert(sizeof(Config) == kConfigBytes, "Config must be 128 bytes");

inline void ConfigSetDefaults(Config &c)
{
	memset(&c, 0xFF, sizeof(c));
	c.magic = kConfigMagic;
	c.version = kConfigVersion;
	c.engine_mode = 0;
	c.active_song = 0;
	c.tempo_bpm_x10 = 1200;
	c.swing = 50;
	c.out_src_bits = 0;      // all local
	c.fwd_enable = 0x0F;     // forward everything when linked
	c.pulse1_lane = 0;       // kick
	c.pulse2_lane = 2;       // snare
	c.cv2_lane = 0;
	c.out2_lane_mask = 0x0001;
	c.release_lead = 15;     // 60 ms
	c.release_bass = 30;     // 120 ms
	c.master_vol = 200;
	c.release_pad = 60;      // 240 ms
	memset(c.midi_channel_to_part, 0xFF, sizeof(c.midi_channel_to_part));
	c.midi_channel_to_part[0] = 0;  // ch1 -> lead
	c.midi_channel_to_part[1] = 1;  // ch2 -> bass
	c.midi_channel_to_part[2] = 3;  // ch3 -> pad
	c.midi_channel_to_part[9] = 2;  // ch10 -> drums
}

// Clamp fields that arrive over the wire into safe ranges.
inline void ConfigSanitize(Config &c, uint32_t songSlots)
{
	if (c.engine_mode > 1) c.engine_mode = 0;
	if (c.active_song >= songSlots) c.active_song = 0;
	if (c.tempo_bpm_x10 < 400) c.tempo_bpm_x10 = 400;
	if (c.tempo_bpm_x10 > 2400) c.tempo_bpm_x10 = 2400;
	if (c.swing < 50) c.swing = 50;
	if (c.swing > 75) c.swing = 75;
	if (c.pulse1_lane > 15) c.pulse1_lane = 0;
	if (c.pulse2_lane > 15) c.pulse2_lane = 2;
	if (c.cv2_lane > 15) c.cv2_lane = 0;
	if (c.release_pad == 0) c.release_pad = 60; // byte was reserved-0 in old blobs
	for (int i = 0; i < 16; i++)
		if (c.midi_channel_to_part[i] > 3 && c.midi_channel_to_part[i] != 0xFF)
			c.midi_channel_to_part[i] = 0xFF;
}

} // namespace cowork

#endif
