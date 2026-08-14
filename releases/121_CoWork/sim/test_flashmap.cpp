// FlashMap region math (2 MB / 16 MB), format validation, BankInfo
// parsing, CRC32 vectors, and the MIDI stream parser.

#include "test_common.h"
#include "../src/storage/FlashMap.h"
#include "../src/storage/ConfigFormat.h"
#include "../src/storage/BankInfo.h"
#include "../src/link/MidiParser.h"

#include <vector>

using namespace cowork;

static void TestMap16MB()
{
	FlashMap m = FlashMap::Compute(16u * 1024u * 1024u);
	CHECK_EQ(m.configOff, 0x040000u);
	CHECK_EQ(m.songsOff, 0x041000u);
	CHECK_EQ(m.songSlots, 8u);
	CHECK_EQ(m.bankOff, 0x0C1000u);
	CHECK_EQ(m.bankDataOff, 0x0C2000u);
	CHECK_EQ(m.bankEnd, 0x1000000u);
	// ~15.2 MB of sample data (docs/FORMATS.md)
	CHECK(m.BankDataBytes() > 15u * 1024u * 1024u);
}

static void TestMap2MB()
{
	FlashMap m = FlashMap::Compute(2u * 1024u * 1024u);
	CHECK_EQ(m.songSlots, 4u);
	CHECK_EQ(m.bankOff, 0x081000u);
	CHECK_EQ(m.bankEnd, 0x200000u);
	CHECK(m.BankDataBytes() > 1400u * 1024u);
}

static void TestWritableRegions()
{
	FlashMap m = FlashMap::Compute(16u * 1024u * 1024u);
	// Firmware + config are NOT web-writable
	CHECK(!m.InWritableRegion(0, 4096));
	CHECK(!m.InWritableRegion(m.configOff, 4096));
	// Songs and bank are
	CHECK(m.InWritableRegion(m.songsOff, 4096));
	CHECK(m.InWritableRegion(m.SongSlotOff(7), m.songSlotBytes));
	CHECK(m.InWritableRegion(m.bankOff, 4096));
	CHECK(m.InWritableRegion(m.bankEnd - 4096, 4096));
	// Overrun / overflow rejected
	CHECK(!m.InWritableRegion(m.bankEnd - 4096, 8192));
	CHECK(!m.InWritableRegion(0xFFFFF000u, 0x2000u));
	CHECK(!m.InWritableRegion(m.songsOff, 0));
	// A range straddling songs->bank is fine (both writable, contiguous)?
	// No: our rule checks region containment, so straddles are rejected.
	CHECK(!m.InWritableRegion(m.bankOff - 4096, 8192));
}

static void TestConfigDefaults()
{
	Config c;
	ConfigSetDefaults(c);
	CHECK_EQ(c.magic, kConfigMagic);
	CHECK_EQ(c.tempo_bpm_x10, 1200);
	CHECK_EQ(c.midi_channel_to_part[0], 0);
	CHECK_EQ(c.midi_channel_to_part[2], 3);  // ch3 -> pad
	CHECK_EQ(c.midi_channel_to_part[9], 2);
	CHECK_EQ(c.midi_channel_to_part[3], 0xFF);
	CHECK_EQ(c.release_pad, 60);

	c.swing = 90;
	c.active_song = 200;
	c.engine_mode = 7;
	ConfigSanitize(c, 8);
	CHECK_EQ(c.swing, 75);
	CHECK_EQ(c.active_song, 0);
	CHECK_EQ(c.engine_mode, 0);
}

static void TestCrc32Vector()
{
	const char *s = "123456789";
	CHECK_EQ(Crc32::Compute((const uint8_t *)s, 9), 0xCBF43926u);
	// Incremental == one-shot
	uint32_t st = Crc32::Begin();
	st = Crc32::Update(st, (const uint8_t *)s, 4);
	st = Crc32::Update(st, (const uint8_t *)s + 4, 5);
	CHECK_EQ(Crc32::Final(st), 0xCBF43926u);
}

// Build a full flash image in RAM with a valid bank + one song
static void TestBankInfoParsing()
{
	FlashMap m = FlashMap::Compute(2u * 1024u * 1024u);
	std::vector<uint8_t> flash(m.flashBytes, 0xFF);

	// Song in slot 1
	SongBuilder sb;
	sb.NoteOn(0, kPartDrums, 36, 100);
	auto song = sb.Build();
	memcpy(flash.data() + m.SongSlotOff(1), song.data(), song.size());

	// Sample bank: lead at bank+0x1000, kick drum at bank+0x2000
	BankHeader bh{};
	bh.magic = kBankMagic;
	bh.version = kBankVersion;
	bh.slot_count = kBankSlots;
	bh.sample_rate = kBankSampleRate;

	SampleSlot slots[kBankSlots];
	memset(slots, 0xFF, sizeof(slots));
	for (auto &s : slots) { s.length_frames = 0; s.offset = 0xFFFFFFFF; }

	slots[kSlotLead] = SampleSlot{ "lead", 0x1000, 1000, 24000, 60, 0xFF, 1, 0, 0 };
	slots[kSlotPad] = SampleSlot{ "pad", 0x4000, 800, 24000, 62, 0xFF, 1, 0, 0 };
	slots[kSlotDrum0] = SampleSlot{ "kick", 0x2000, 500, 24000, 0xFF, 36, 0, 0, 0 };
	// Bad slot: unaligned offset -> skipped
	slots[kSlotDrum0 + 1] = SampleSlot{ "bad", 0x2100, 500, 24000, 0xFF, 38, 0, 0, 0 };
	// Bad slot: runs past the bank -> skipped
	slots[kSlotDrum0 + 2] = SampleSlot{ "big", 0x3000, 0x7FFFFFFF, 24000, 0xFF, 40, 0, 0, 0 };

	memcpy(flash.data() + m.bankOff, &bh, sizeof(bh));
	memcpy(flash.data() + m.bankOff + sizeof(bh), slots, sizeof(slots));

	BankInfo info;
	BuildBankInfo(info, flash.data(), m);

	CHECK(info.bankValid);
	CHECK_EQ(info.songSlots, 4u);
	CHECK(!info.song[0].valid);
	CHECK(info.song[1].valid);
	CHECK(info.lead.data != nullptr);
	CHECK_EQ(info.lead.frames, 1000u);
	CHECK(info.lead.loop);
	CHECK(info.pad.data != nullptr);
	CHECK_EQ(info.pad.frames, 800u);
	CHECK_EQ(info.pad.root, 62);
	CHECK(info.lane[0].data != nullptr);
	CHECK_EQ(info.noteToLane[36], 0);
	CHECK_EQ(info.noteToLane[38], 0xFF); // bad slot skipped
	CHECK_EQ(info.noteToLane[40], 0xFF); // oversized slot skipped
	CHECK(info.bass.data == nullptr);

	// Erased bank -> invalid but song list still parsed
	std::vector<uint8_t> erased(m.flashBytes, 0xFF);
	BankInfo info2;
	BuildBankInfo(info2, erased.data(), m);
	CHECK(!info2.bankValid);
	CHECK(info2.lead.data == nullptr);
}

struct MidiCapture : MidiParser::Sink {
	std::vector<uint8_t> realtime;
	std::vector<uint32_t> msgs; // status<<16 | d1<<8 | d2
	void OnRealtime(uint8_t s) override { realtime.push_back(s); }
	void OnMessage(uint8_t s, uint8_t d1, uint8_t d2) override
	{
		msgs.push_back(((uint32_t)s << 16) | ((uint32_t)d1 << 8) | d2);
	}
};

static void TestMidiParser()
{
	MidiParser p;
	MidiCapture c;

	// Running status: one status byte, two note-ons
	const uint8_t runStat[] = { 0x90, 0x3C, 0x7F, 0x40, 0x7F };
	p.Feed(runStat, sizeof(runStat), c);
	CHECK_EQ(c.msgs.size(), 2u);
	CHECK_EQ(c.msgs[0], 0x903C7Fu);
	CHECK_EQ(c.msgs[1], 0x90407Fu); // status persists

	// Realtime interleaved mid-message
	c = MidiCapture{};
	p.Reset();
	const uint8_t inter[] = { 0x90, 0x3C, 0xF8, 0x7F };
	p.Feed(inter, sizeof(inter), c);
	CHECK_EQ(c.realtime.size(), 1u);
	CHECK_EQ(c.realtime[0], 0xF8);
	CHECK_EQ(c.msgs.size(), 1u);
	CHECK_EQ(c.msgs[0], 0x903C7Fu);

	// SysEx skipped, message after survives
	c = MidiCapture{};
	p.Reset();
	const uint8_t syx[] = { 0xF0, 0x7D, 0x01, 0x02, 0xF7, 0xB0, 0x14, 0x40 };
	p.Feed(syx, sizeof(syx), c);
	CHECK_EQ(c.msgs.size(), 1u);
	CHECK_EQ(c.msgs[0], 0xB01440u);

	// 2-byte message (program change)
	c = MidiCapture{};
	p.Reset();
	const uint8_t pc[] = { 0xC5, 0x07 };
	p.Feed(pc, sizeof(pc), c);
	CHECK_EQ(c.msgs.size(), 1u);
	CHECK_EQ(c.msgs[0], 0xC50700u);
}

int main()
{
	TestMap16MB();
	TestMap2MB();
	TestWritableRegions();
	TestConfigDefaults();
	TestCrc32Vector();
	TestBankInfoParsing();
	TestMidiParser();
	return TestResult("test_flashmap");
}
