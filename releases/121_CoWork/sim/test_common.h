// Minimal host-side test harness + CWSQ/CWSB image builders.

#ifndef COWORK_TEST_COMMON_H
#define COWORK_TEST_COMMON_H

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

#include "../src/seq/SeqFormat.h"
#include "../src/storage/Crc32.h"
#include "../src/storage/SampleBankFormat.h"

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond) do { \
	gChecks++; \
	if (!(cond)) { \
		gFailures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	gChecks++; \
	auto va = (a); auto vb = (b); \
	if (!(va == vb)) { \
		gFailures++; \
		printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, \
		       #a, #b, (long long)va, (long long)vb); \
	} \
} while (0)

static inline int TestResult(const char *name)
{
	printf("%s: %d checks, %d failures\n", name, gChecks, gFailures);
	return gFailures ? 1 : 0;
}

// ---- CWSQ builder ----

struct SongBuilder {
	std::vector<cowork::SeqEvent> events;
	uint32_t lengthTicks = 384; // one 4/4 bar
	uint32_t loopStart = 0;
	uint32_t loopEnd = 384;
	uint16_t flags = 1; // loop enabled
	uint32_t tempoUspq = 500000;
	uint8_t tsigNum = 4;

	void NoteOn(uint32_t tick, uint8_t part, uint8_t note, uint8_t vel)
	{
		events.push_back({ tick, cowork::kEvNoteOn, part, note, vel });
	}
	void NoteOff(uint32_t tick, uint8_t part, uint8_t note)
	{
		events.push_back({ tick, cowork::kEvNoteOff, part, note, 0 });
	}
	void Tempo(uint32_t tick, uint32_t uspq)
	{
		events.push_back({ tick, cowork::kEvTempo,
			(uint8_t)((uspq >> 16) & 0xFF), (uint8_t)((uspq >> 8) & 0xFF),
			(uint8_t)(uspq & 0xFF) });
	}

	std::vector<uint8_t> Build()
	{
		events.push_back({ lengthTicks, cowork::kEvEnd, 0, 0, 0 });
		cowork::SeqHeader h{};
		h.magic = cowork::kSeqMagic;
		h.version = cowork::kSeqVersion;
		h.flags = flags;
		h.ppqn = cowork::kPPQN;
		h.tsig_num = tsigNum;
		h.tsig_denom_log2 = 2;
		h.init_tempo_uspq = tempoUspq;
		h.length_ticks = lengthTicks;
		h.loop_start_tick = loopStart;
		h.loop_end_tick = loopEnd;
		h.event_count = (uint32_t)events.size();
		strncpy(h.name, "test", sizeof(h.name));
		h.events_crc32 = cowork::Crc32::Compute(
			(const uint8_t *)events.data(),
			events.size() * sizeof(cowork::SeqEvent));
		h.reserved = 0xFFFFFFFF;

		std::vector<uint8_t> out(sizeof(h) + events.size() * sizeof(cowork::SeqEvent));
		memcpy(out.data(), &h, sizeof(h));
		memcpy(out.data() + sizeof(h), events.data(),
		       events.size() * sizeof(cowork::SeqEvent));
		return out;
	}
};

// Record of dispatched events for assertions
struct EventLog {
	struct Entry {
		uint32_t sample;
		char kind; // 'n' on, 'f' off, 't' tempo, 'w' wrap
		uint8_t part, d1, d2;
		uint32_t tempo;
	};
	std::vector<Entry> entries;
};

#endif
