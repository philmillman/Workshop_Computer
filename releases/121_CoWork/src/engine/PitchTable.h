// MIDI note -> Q12 phase increment for 24 kHz source played at 48 kHz.
// Unity pitch (note == root) is 2048 (= 0.5 in Q12: one source sample per
// two output samples). Pure header, host-testable.
//
// kSemitoneSteps is 2^(n/12) in Q12 (Fragments precedent).

#ifndef COWORK_PITCH_TABLE_H
#define COWORK_PITCH_TABLE_H

#include <cstdint>

namespace cowork {

constexpr uint32_t kUnityIncQ12 = 2048; // 24 kHz source at 48 kHz output

constexpr uint32_t kSemitoneSteps[12] = {
	4096, 4340, 4598, 4871, 5161, 5468,
	5793, 6137, 6502, 6889, 7298, 7732,
};

// Phase increment for playing `note` on a sample recorded at `root`.
// Clamped to ±4 octaves from root; result fits comfortably in 32 bits.
inline uint32_t PitchIncQ12(int note, int root)
{
	int rel = note - root;
	if (rel < -48) rel = -48;
	if (rel > 48) rel = 48;
	int oct = 0;
	while (rel < 0) { rel += 12; oct--; }
	oct += rel / 12;
	int st = rel % 12;
	uint32_t inc = (kUnityIncQ12 * kSemitoneSteps[st]) >> 12;
	if (oct >= 0) inc <<= oct;
	else inc >>= -oct;
	if (inc == 0) inc = 1;
	return inc;
}

} // namespace cowork

#endif
