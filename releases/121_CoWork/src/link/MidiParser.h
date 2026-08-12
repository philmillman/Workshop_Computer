// Byte-stream MIDI parser: realtime bytes fire immediately (they may be
// interleaved anywhere, even mid-message), channel voice messages support
// running status, SysEx is skipped. Pure header, host-testable.

#ifndef COWORK_MIDI_PARSER_H
#define COWORK_MIDI_PARSER_H

#include <cstdint>

namespace cowork {

class MidiParser {
public:
	struct Sink {
		virtual void OnRealtime(uint8_t status) = 0;
		// Complete channel voice message (status includes channel nibble).
		// 2-byte messages arrive with d2 = 0.
		virtual void OnMessage(uint8_t status, uint8_t d1, uint8_t d2) = 0;
	};

	void Feed(uint8_t b, Sink &sink)
	{
		if (b >= 0xF8) { sink.OnRealtime(b); return; }

		if (b == 0xF0) { inSysEx_ = true; runningStatus_ = 0; return; }
		if (b == 0xF7) { inSysEx_ = false; return; }
		if (inSysEx_) return;

		if (b & 0x80) {
			if (b >= 0xF0) { runningStatus_ = 0; needed_ = 0; return; } // other system common
			runningStatus_ = b;
			count_ = 0;
			needed_ = LenFor(b);
			return;
		}

		// Data byte
		if (runningStatus_ == 0) return;
		data_[count_++] = b;
		if (count_ >= needed_) {
			sink.OnMessage(runningStatus_, data_[0], needed_ > 1 ? data_[1] : 0);
			count_ = 0; // running status persists
		}
	}

	void Feed(const uint8_t *buf, uint32_t len, Sink &sink)
	{
		for (uint32_t i = 0; i < len; i++) Feed(buf[i], sink);
	}

	void Reset()
	{
		runningStatus_ = 0;
		count_ = 0;
		needed_ = 0;
		inSysEx_ = false;
	}

private:
	static uint8_t LenFor(uint8_t status)
	{
		switch (status & 0xF0) {
		case 0xC0: case 0xD0: return 1; // program change, channel pressure
		default: return 2;
		}
	}

	uint8_t runningStatus_ = 0;
	uint8_t data_[2] = {0, 0};
	uint8_t count_ = 0;
	uint8_t needed_ = 0;
	bool inSysEx_ = false;
};

} // namespace cowork

#endif
