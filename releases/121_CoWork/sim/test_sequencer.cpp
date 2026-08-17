// Sequencer: validation, event timing, loop wrap, tempo events, seek.

#include "test_common.h"
#include "../src/seq/Sequencer.h"

using namespace cowork;

struct Recorder : Sequencer::Listener {
	EventLog log;
	uint32_t sample = 0;
	void OnNoteOn(uint8_t part, uint8_t note, uint8_t vel) override
	{
		log.entries.push_back({ sample, 'n', part, note, vel, 0 });
	}
	void OnNoteOff(uint8_t part, uint8_t note) override
	{
		log.entries.push_back({ sample, 'f', part, note, 0, 0 });
	}
	void OnTempo(uint32_t uspq) override
	{
		log.entries.push_back({ sample, 't', 0, 0, 0, uspq });
	}
	void OnLoopWrap() override
	{
		log.entries.push_back({ sample, 'w', 0, 0, 0, 0 });
	}
};

static void TestValidation()
{
	SongBuilder b;
	b.NoteOn(0, kPartLead, 60, 100);
	auto img = b.Build();

	Sequencer s;
	CHECK(s.Attach(img.data(), (uint32_t)img.size()));
	CHECK(s.Loaded());

	// Wrong magic
	auto bad = img;
	bad[0] = 'X';
	CHECK(!s.Attach(bad.data(), (uint32_t)bad.size()));

	// Truncated (event_count larger than the slot)
	CHECK(!s.Attach(img.data(), sizeof(SeqHeader) + 4));

	// Erased flash
	std::vector<uint8_t> erased(4096, 0xFF);
	CHECK(!s.Attach(erased.data(), (uint32_t)erased.size()));
}

static void TestTiming()
{
	SongBuilder b;
	b.flags = 0; // no loop
	b.NoteOn(0, kPartLead, 60, 100);
	b.NoteOff(48, kPartLead, 60);   // an eighth note at 96 PPQN
	b.NoteOn(96, kPartBass, 36, 90);
	auto img = b.Build();

	Sequencer s;
	CHECK(s.Attach(img.data(), (uint32_t)img.size()));
	s.SetTickIncQ16(Sequencer::TickIncForTempo(500000)); // 120 BPM

	Recorder r;
	bool ended = false;
	for (r.sample = 0; r.sample < 300000 && !ended; r.sample++)
		if (s.Advance(r) & Sequencer::kAdvEnded) ended = true;

	CHECK(ended);
	CHECK_EQ(r.log.entries.size(), 3u);
	CHECK_EQ(r.log.entries[0].kind, 'n');
	CHECK(r.log.entries[0].sample <= 2);
	// 48 ticks at 120 BPM = 0.25 s = 12000 samples (inc rounds down: allow 1% slack)
	CHECK(r.log.entries[1].kind == 'f');
	CHECK(r.log.entries[1].sample >= 12000 && r.log.entries[1].sample <= 12150);
	CHECK(r.log.entries[2].kind == 'n');
	CHECK(r.log.entries[2].sample >= 24000 && r.log.entries[2].sample <= 24300);
}

static void TestLoop()
{
	SongBuilder b;
	b.lengthTicks = 96;
	b.loopEnd = 96;
	b.flags = 1;
	b.NoteOn(0, kPartDrums, 36, 100);
	auto img = b.Build();

	Sequencer s;
	CHECK(s.Attach(img.data(), (uint32_t)img.size()));
	s.SetTickIncQ16(Sequencer::TickIncForTempo(500000));

	Recorder r;
	// 2.5 quarters at 120 BPM = 1.25 s = 60000 samples
	for (r.sample = 0; r.sample < 60000; r.sample++)
		s.Advance(r);

	int ons = 0, wraps = 0;
	for (auto &e : r.log.entries) {
		if (e.kind == 'n') ons++;
		if (e.kind == 'w') wraps++;
	}
	CHECK_EQ(ons, 3);
	CHECK_EQ(wraps, 2);
	CHECK(s.Tick() < 96);
}

static void TestTempoEvent()
{
	SongBuilder b;
	b.flags = 0;
	b.Tempo(0, 400000); // 150 BPM
	b.NoteOn(96, kPartLead, 60, 100);
	auto img = b.Build();

	Sequencer s;
	CHECK(s.Attach(img.data(), (uint32_t)img.size()));
	s.SetTickIncQ16(Sequencer::TickIncForTempo(500000));

	Recorder r;
	for (r.sample = 0; r.sample < 1000; r.sample++)
		s.Advance(r);
	CHECK(!r.log.entries.empty());
	CHECK_EQ(r.log.entries[0].kind, 't');
	CHECK_EQ(r.log.entries[0].tempo, 400000u);
}

static void TestSeek()
{
	SongBuilder b;
	b.lengthTicks = 384;
	b.loopEnd = 384;
	b.NoteOn(0, kPartLead, 60, 100);
	b.NoteOn(192, kPartLead, 62, 100);
	b.NoteOn(288, kPartLead, 64, 100);
	auto img = b.Build();

	Sequencer s;
	CHECK(s.Attach(img.data(), (uint32_t)img.size()));
	s.SetTickIncQ16(Sequencer::TickIncForTempo(500000));
	s.SeekTick((uint64_t)200 << 16);

	Recorder r;
	for (r.sample = 0; r.sample < 48000; r.sample++)
		s.Advance(r);

	// First dispatched note after seeking to tick 200 must be note 64 @288
	CHECK(!r.log.entries.empty());
	CHECK_EQ(r.log.entries[0].d1, 64);

	// Seek past loop end folds into the loop region
	s.SeekTick((uint64_t)500 << 16);
	CHECK(s.Tick() < 384);
}

// Regression (field report): double-tap song advance attaches the next
// song from inside OnLoopWrap — i.e. while Advance() is mid-wrap. The old
// code then subtracted the OLD loop span from the freshly reset phase,
// underflowing it so no events ever dispatched again until a transport
// restart. Attach from the callback must leave the new song playing.
struct SwitchOnWrap : Recorder {
	Sequencer *seq = nullptr;
	const uint8_t *next = nullptr;
	uint32_t nextBytes = 0;
	bool switched = false;
	void OnLoopWrap() override
	{
		Recorder::OnLoopWrap();
		if (!switched) {
			switched = true;
			seq->Attach(next, nextBytes);
			seq->SetTickIncQ16(Sequencer::TickIncForTempo(500000));
		}
	}
};

static void TestSongSwitchInsideWrapCallback()
{
	SongBuilder a;
	a.lengthTicks = 96;
	a.loopEnd = 96;
	a.NoteOn(0, kPartLead, 60, 100);
	auto imgA = a.Build();

	SongBuilder b;
	b.lengthTicks = 192;
	b.loopEnd = 192;
	b.NoteOn(0, kPartLead, 72, 100);
	b.NoteOn(96, kPartLead, 74, 100);
	auto imgB = b.Build();

	Sequencer s;
	CHECK(s.Attach(imgA.data(), (uint32_t)imgA.size()));
	s.SetTickIncQ16(Sequencer::TickIncForTempo(500000));

	SwitchOnWrap r;
	r.seq = &s;
	r.next = imgB.data();
	r.nextBytes = (uint32_t)imgB.size();

	// One quarter of song A (to its wrap/switch at ~24k samples), then
	// enough of song B to pass its tick-96 note (~24k more)
	for (r.sample = 0; r.sample < 60000; r.sample++) {
		s.Advance(r);
		CHECK(s.Tick() < 192); // phase must never explode
		if (gFailures) return; // don't spam on failure
	}

	CHECK(r.switched);
	// Note 60 (song A), then song B's 72 and 74 must both have played
	int saw60 = 0, saw72 = 0, saw74 = 0;
	for (auto &e : r.log.entries)
		if (e.kind == 'n') {
			if (e.d1 == 60) saw60++;
			if (e.d1 == 72) saw72++;
			if (e.d1 == 74) saw74++;
		}
	CHECK(saw60 >= 1);
	CHECK(saw72 >= 1);
	CHECK(saw74 >= 1);
}

static void TestTickIncMath()
{
	// 120 BPM at 96 PPQN = 192 ticks/s = 0.004 ticks/sample = 262.144 Q16
	CHECK_EQ(Sequencer::TickIncForTempo(500000), 262u);
	CHECK_EQ(Sequencer::TickIncForBpmX10(1200), 262u);
	// Clamps
	CHECK(Sequencer::TickIncForTempo(1) == Sequencer::TickIncForTempo(100000));
	CHECK(Sequencer::TickIncForTempo(0xFFFFFFFF) == Sequencer::TickIncForTempo(6000000));
}

int main()
{
	TestValidation();
	TestTiming();
	TestLoop();
	TestTempoEvent();
	TestSeek();
	TestSongSwitchInsideWrapCallback();
	TestTickIncMath();
	return TestResult("test_sequencer");
}
