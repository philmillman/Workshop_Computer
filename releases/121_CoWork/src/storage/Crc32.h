// IEEE 802.3 CRC32 (poly 0xEDB88320), matching the web side's implementation.
// Table built lazily in RAM (1 KB). Pure header, host-testable.

#ifndef COWORK_CRC32_H
#define COWORK_CRC32_H

#include <cstdint>
#include <cstddef>

namespace cowork {

class Crc32 {
public:
	// Incremental interface: Begin() -> Update(...) -> Final()
	static uint32_t Begin() { return 0xFFFFFFFFu; }

	static uint32_t Update(uint32_t state, const uint8_t *data, size_t len)
	{
		const uint32_t *t = Table();
		for (size_t i = 0; i < len; i++)
			state = t[(state ^ data[i]) & 0xFF] ^ (state >> 8);
		return state;
	}

	static uint32_t Final(uint32_t state) { return state ^ 0xFFFFFFFFu; }

	static uint32_t Compute(const uint8_t *data, size_t len)
	{
		return Final(Update(Begin(), data, len));
	}

private:
	static const uint32_t *Table()
	{
		static uint32_t table[256];
		static bool built = false;
		if (!built) {
			for (uint32_t n = 0; n < 256; n++) {
				uint32_t c = n;
				for (int k = 0; k < 8; k++)
					c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
				table[n] = c;
			}
			built = true;
		}
		return table;
	}
};

} // namespace cowork

#endif
