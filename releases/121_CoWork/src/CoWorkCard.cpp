#include "CoWorkCard.h"
#include "link/midi_defs.h"

#include "pico/stdlib.h"

namespace cowork {

// ===================== Sequencer::Listener =====================

void CoWorkCard::OnNoteOn(uint8_t part, uint8_t note, uint8_t vel)
{
	if (cfg_.engine_mode == 1) {
		if (part != kPartDrums || !bank_) return;
		uint8_t lane = bank_->noteToLane[note & 0x7F];
		if (lane == 0xFF) return;
		engine_.DrumTrigger(lane, vel);
		accentVel_ = vel;
		if (lane == cfg_.cv2_lane) cv2LaneVel_ = vel;
		if (lane == cfg_.pulse1_lane) pulseTimer_[0] = kDrumPulseSamples;
		if (lane == cfg_.pulse2_lane) pulseTimer_[1] = kDrumPulseSamples;
	} else {
		if (part == kPartLead) {
			engine_.NoteOn(kPartLead, note, vel);
			leadGate_++;
			lastLeadNote_ = note;
		} else if (part == kPartBass) {
			engine_.NoteOn(kPartBass, note, vel);
			bassGate_++;
			lastBassNote_ = note;
		}
	}
}

void CoWorkCard::OnNoteOff(uint8_t part, uint8_t note)
{
	if (cfg_.engine_mode == 1) return; // drum one-shots ignore offs
	if (part == kPartLead) {
		engine_.NoteOff(kPartLead, note);
		if (leadGate_ > 0) leadGate_--;
	} else if (part == kPartBass) {
		engine_.NoteOff(kPartBass, note);
		if (bassGate_ > 0) bassGate_--;
	}
}

void CoWorkCard::OnTempo(uint32_t uspq)
{
	// The leader honors the file's tempo map until the knob takes over;
	// the follower slaves to received clock and ignores TEMPO events.
	if (role_ == kLeader && !tempoKnobPicked_)
		tempoUspq_ = uspq;
}

void CoWorkCard::OnLoopWrap()
{
	// Notes that straddle the loop point get their offs from the encoder's
	// dangling-off injection; release everything as a safety net.
	engine_.ReleaseAll();
	ClearGates();
	if (pendingSong_ != 0xFF) {
		uint8_t s = pendingSong_;
		pendingSong_ = 0xFF;
		AttachSong(s);
	}
}

// ===================== transport helpers =====================

void CoWorkCard::ClearGates()
{
	leadGate_ = 0;
	bassGate_ = 0;
	pulseTimer_[0] = pulseTimer_[1] = 0;
}

void CoWorkCard::StartTransport(bool resetPosition)
{
	if (pendingSong_ != 0xFF && !running_) {
		uint8_t s = pendingSong_;
		pendingSong_ = 0xFF;
		AttachSong(s);
	}
	if (resetPosition) {
		seq_.Reset();
		ClearGates();
	}
	lastClockTick_ = 0xFFFFFFFF; // re-arm the tick-0 clock
	running_ = true;
	gShared.transportRunning = 1;
}

void CoWorkCard::StopTransport()
{
	running_ = false;
	internalRun_ = false;
	gShared.transportRunning = 0;
	engine_.ReleaseAll();
	ClearGates();
	lastStopUs_ = nowUs_;
}

void CoWorkCard::AttachSong(uint32_t slot)
{
	if (!bank_ || slot >= bank_->songSlots) return;
	gShared.statusSong = slot;
	if (bank_->song[slot].valid) {
		seq_.Attach(bank_->song[slot].slot, bank_->song[slot].bytes);
		if (seq_.Loaded())
			tempoUspq_ = seq_.Header()->init_tempo_uspq;
	} else {
		seq_.Detach();
	}
}

void CoWorkCard::ApplyEngineMode(uint8_t mode)
{
	engine_.KillAll();
	ClearGates();
	engine_.SetPercussive(mode == 1);
	cfg_.engine_mode = mode;
	gShared.statusMode = mode;
}

// ===================== per-sample stages =====================

void CoWorkCard::LatchRoleAtBoot()
{
	role_ = (SwitchVal() == Switch::Up) ? kLeader : kFollower;
	gShared.statusRole = (role_ == kFollower) ? 1 : 0;
	roleLatched_ = true;
	bootKnobX_ = KnobVal(Knob::X);
	bootKnobMain_ = KnobVal(Knob::Main);
	clock_.Reset(cfg_.tempo_bpm_x10);
}

bool CoWorkCard::HandleQuiesce()
{
	if (gShared.flashMutateReq) {
		if (!gShared.flashQuiesced) {
			// Stop touching XIP: kill voices, park the sequencer.
			engine_.KillAll();
			ClearGates();
			gShared.flashQuiesced = 1;
		}
		AudioOut1(0);
		AudioOut2(0);
		PulseOut1(false);
		PulseOut2(false);
		UpdateLeds();
		return true;
	}
	if (gShared.flashQuiesced)
		gShared.flashQuiesced = 0;
	return false;
}

void CoWorkCard::AdoptBankIfChanged()
{
	uint32_t seq = gShared.bankSeq;
	if (seq == lastBankSeq_) return;
	lastBankSeq_ = seq;
	bank_ = &ActiveBank(seq);

	// Sample data may have moved: silence everything, repoint, reattach.
	engine_.KillAll();
	ClearGates();
	engine_.SetInstrument(kPartLead, bank_->lead.data, bank_->lead.frames,
	                      bank_->lead.root, bank_->lead.loop);
	engine_.SetInstrument(kPartBass, bank_->bass.data, bank_->bass.frames,
	                      bank_->bass.root, bank_->bass.loop);
	for (uint32_t i = 0; i < kNumDrumLanes; i++)
		engine_.SetDrumLane(i, bank_->lane[i].data, bank_->lane[i].frames,
		                    bank_->lane[i].choke);
	AttachSong(cfg_.active_song < bank_->songSlots ? cfg_.active_song : 0);
}

void CoWorkCard::AdoptConfigIfChanged()
{
	uint32_t seq = gShared.configApplySeq;
	if (seq == lastCfgSeq_) return;
	lastCfgSeq_ = seq;

	Config next = gShared.configStaging; // 128-byte copy, ~1 us

	bool modeChanged = next.engine_mode != cfg_.engine_mode;
	bool songChanged = next.active_song != cfg_.active_song;
	cfg_ = next;

	engine_.SetReleaseMs(kPartLead, (int)cfg_.release_lead * 4);
	engine_.SetReleaseMs(kPartBass, (int)cfg_.release_bass * 4);
	engine_.SetOut2LaneMask(cfg_.out2_lane_mask);
	if (modeChanged) ApplyEngineMode(cfg_.engine_mode);
	gShared.statusMode = cfg_.engine_mode;
	if (songChanged) {
		if (running_) pendingSong_ = cfg_.active_song;
		else AttachSong(cfg_.active_song);
	}
}

void CoWorkCard::ConsumeMidiIn()
{
	MidiInEvent ev;
	int budget = 8;
	while (budget-- > 0 && gShared.midiIn.Pop(ev)) {
		if (ev.status >= 0xF8) {
			// Realtime
			if (role_ == kLeader) {
				// Someone else is sending clock at us: dual leader.
				if (ev.status == kMidiClock || ev.status == kMidiStart)
					conflictUntilUs_ = ev.tUs + 2000000;
				continue;
			}
			switch (ev.status) {
			case kMidiClock:
				clock_.OnClock(ev.tUs, seq_.PhaseQ16());
				internalRun_ = false;
				break;
			case kMidiStart:
				// DAW loop-wrap debounce (clockwork): a Start right
				// after a Stop is a wrap, not a restart.
				if (ev.tUs - lastStopUs_ > 100000) {
					clock_.OnStart();
					seq_.Reset();
				}
				engine_.ReleaseAll();
				ClearGates();
				internalRun_ = false;
				StartTransport(false);
				break;
			case kMidiContinue:
				internalRun_ = false;
				StartTransport(false);
				break;
			case kMidiStop:
				StopTransport();
				break;
			default:
				break;
			}
			continue;
		}

		uint8_t type = ev.status & 0xF0;
		uint8_t chan = ev.status & 0x0F;

		if (chan == kLinkChannel) {
			// Peer-forwarded CV / pulses
			if (type == 0xB0) {
				switch (ev.d1) {
				case kCcCv1Lsb: remoteCvLsb_[0] = ev.d2; break;
				case kCcCv2Lsb: remoteCvLsb_[1] = ev.d2; break;
				case kCcCv1Msb:
					remoteCv_[0] = CvFrom14(((uint16_t)ev.d2 << 7) | remoteCvLsb_[0]);
					remoteCvAtUs_[0] = ev.tUs;
					break;
				case kCcCv2Msb:
					remoteCv_[1] = CvFrom14(((uint16_t)ev.d2 << 7) | remoteCvLsb_[1]);
					remoteCvAtUs_[1] = ev.tUs;
					break;
				default: break;
				}
			} else if (type == 0x90 || type == 0x80) {
				bool on = (type == 0x90) && ev.d2 > 0;
				if (ev.d1 == kNotePulse1) { remotePulse_[0] = on; remotePulseAtUs_[0] = ev.tUs; }
				if (ev.d1 == kNotePulse2) { remotePulse_[1] = on; remotePulseAtUs_[1] = ev.tUs; }
			}
			continue;
		}

		// Live USB-MIDI notes on mapped channels play the engine directly
		if (type == 0x90 || type == 0x80) {
			uint8_t part = cfg_.midi_channel_to_part[chan];
			if (part > 2) continue;
			bool on = (type == 0x90) && ev.d2 > 0;
			if (part == kPartDrums) {
				if (on && cfg_.engine_mode == 1 && bank_) {
					uint8_t lane = bank_->noteToLane[ev.d1 & 0x7F];
					if (lane != 0xFF) engine_.DrumTrigger(lane, ev.d2);
				}
			} else if (cfg_.engine_mode == 0) {
				if (on) engine_.NoteOn(part, ev.d1, ev.d2);
				else engine_.NoteOff(part, ev.d1);
			}
		}
	}
}

void CoWorkCard::HandleTransportRequests()
{
	if (gShared.transportStopReq) {
		gShared.transportStopReq = 0;
		if (running_) {
			StopTransport();
			if (role_ == kLeader) PushMidiOut(kMidiStop, 0, 0, 1);
		}
	}
	if (gShared.transportPlayReq) {
		gShared.transportPlayReq = 0;
		if (!running_ && role_ == kLeader) {
			StartTransport(true);
			PushMidiOut(kMidiStart, 0, 0, 1);
		}
	}
}

void CoWorkCard::HandleSwitch()
{
	Switch sw = SwitchVal();

	if (sw == Switch::Down) {
		// Require the switch to have been seen elsewhere first, so a
		// switch held during power-up doesn't fire a spurious tap.
		if (!downArmed_) return;
		downCount_++;
		if (downCount_ == kLongPressSamples && !longPressHandled_) {
			longPressHandled_ = true;
			ApplyEngineMode(cfg_.engine_mode ^ 1);
		}
		return;
	}

	downArmed_ = true;
	if (downCount_ > 0 && !longPressHandled_) {
		// Short press: transport toggle
		if (role_ == kLeader) {
			if (running_) {
				StopTransport();
				PushMidiOut(kMidiStop, 0, 0, 1);
			} else {
				StartTransport(true);
				PushMidiOut(kMidiStart, 0, 0, 1);
			}
		} else {
			// Follower: explicit internal-clock fallback when no leader
			if (running_) {
				StopTransport();
			} else if (!clock_.HasClock() ||
			           clock_.TimedOut(nowUs_)) {
				internalRun_ = true;
				seq_.Reset();
				StartTransport(true);
			}
		}
	}
	downCount_ = 0;
	longPressHandled_ = false;
}

void CoWorkCard::HandleKnobs()
{
	// X: tempo (leader) / internal fallback tempo (follower), with pickup
	int32_t kx = KnobVal(Knob::X);
	if (!tempoKnobPicked_ && bootKnobX_ >= 0 &&
	    (kx - bootKnobX_ > kKnobPickup || bootKnobX_ - kx > kKnobPickup))
		tempoKnobPicked_ = true;
	if (tempoKnobPicked_) {
		uint32_t bpmX10 = 400 + ((uint32_t)kx * 2000u) / 4095u;
		if (role_ == kLeader)
			tempoUspq_ = (uint32_t)(600000000ull * 10ull / bpmX10 / 10ull);
		cfg_.tempo_bpm_x10 = (uint16_t)bpmX10;
	}

	// Y: song slot select, bar-latched while running
	if (bank_ && bank_->songSlots > 0) {
		uint32_t slots = bank_->songSlots;
		uint32_t sel = ((uint32_t)KnobVal(Knob::Y) * slots) / 4096u;
		if (sel >= slots) sel = slots - 1;
		if (sel != gShared.statusSong && sel != pendingSong_) {
			if (running_) pendingSong_ = (uint8_t)sel;
			else AttachSong(sel);
		}
	}

	// Main: master volume, config value until the knob moves
	int32_t km = KnobVal(Knob::Main);
	if (!volKnobPicked_ && bootKnobMain_ >= 0 &&
	    (km - bootKnobMain_ > kKnobPickup || bootKnobMain_ - km > kKnobPickup))
		volKnobPicked_ = true;
}

void CoWorkCard::AdvanceSequencer()
{
	if (!running_ || !seq_.Loaded()) return;

	if (role_ == kLeader || internalRun_) {
		uint32_t inc = (role_ == kLeader)
			? Sequencer::TickIncForTempo(tempoUspq_)
			: Sequencer::TickIncForBpmX10(cfg_.tempo_bpm_x10);
		seq_.SetTickIncQ16(inc);
	} else {
		// Follower on external clock
		if (clock_.TimedOut(nowUs_)) {
			StopTransport();
			return;
		}
		if (clock_.NeedsResync()) {
			engine_.ReleaseAll();
			ClearGates();
			seq_.SeekTick(clock_.TargetPhaseQ16());
			clock_.OnResync();
		}
		seq_.SetTickIncQ16(clock_.TickIncQ16());
	}

	uint32_t adv = seq_.Advance(*this);

	// Leader emits MIDI clock at 24 PPQN = every 4th sequencer tick.
	// Checked every sample (not just on tick crossings) so the very first
	// clock — tick 0, which marks the downbeat per the MIDI spec — goes
	// out on the first running sample after Start.
	uint32_t tick = seq_.Tick();
	if (role_ == kLeader && (tick & 3) == 0 && tick != lastClockTick_) {
		lastClockTick_ = tick;
		PushMidiOut(kMidiClock, 0, 0, 1);
	}

	if (adv & Sequencer::kAdvTick) {
		// Beat LED
		if (tick % kPPQN == 0) {
			uint32_t quarters = tick / kPPQN;
			uint32_t perBar = seq_.Loaded() ? seq_.Header()->tsig_num : 4;
			if (perBar == 0) perBar = 4;
			beatLevel_ = (quarters % perBar == 0) ? 4095 : 1600;
		}
	}
	if (adv & Sequencer::kAdvWrapped)
		lastClockTick_ = 0xFFFFFFFF; // ticks restarted from loop_start
	if (adv & Sequencer::kAdvEnded) {
		StopTransport();
		if (role_ == kLeader) PushMidiOut(kMidiStop, 0, 0, 1);
	}
}

void CoWorkCard::RenderAndOutput()
{
	int32_t a = 0, b = 0;
	engine_.Render(a, b);

	// Master volume: knob (after pickup) or config default. Voice sums are
	// ~16-bit; >>4 lands in the 12-bit DAC range.
	int32_t vol = volKnobPicked_ ? KnobVal(Knob::Main)
	                             : ((int32_t)cfg_.master_vol * 4096) / 255;
	a = (a * vol) >> 16;
	b = (b * vol) >> 16;
	if (a > 2047) a = 2047;
	if (a < -2047) a = -2047;
	if (b > 2047) b = 2047;
	if (b < -2047) b = -2047;
	AudioOut1((int16_t)a);
	AudioOut2((int16_t)b);

	// Pulse timers tick down regardless of source selection
	if (pulseTimer_[0]) pulseTimer_[0]--;
	if (pulseTimer_[1]) pulseTimer_[1]--;

	bool remoteFresh[2] = {
		(nowUs_ - remotePulseAtUs_[0]) < kRemoteStaleUs,
		(nowUs_ - remotePulseAtUs_[1]) < kRemoteStaleUs,
	};
	bool remoteCvFresh[2] = {
		(nowUs_ - remoteCvAtUs_[0]) < kRemoteStaleUs,
		(nowUs_ - remoteCvAtUs_[1]) < kRemoteStaleUs,
	};

	// CV outs
	for (int i = 0; i < 2; i++) {
		bool useRemote = (cfg_.out_src_bits & (i == 0 ? kOutCV1 : kOutCV2)) &&
		                 remoteCvFresh[i];
		if (useRemote) {
			CVOut(i, (int16_t)remoteCv_[i]);
		} else if (cfg_.engine_mode == 0) {
			CVOutMIDINote(i, i == 0 ? lastLeadNote_ : lastBassNote_);
		} else {
			uint8_t vel = (i == 0) ? accentVel_ : cv2LaneVel_;
			CVOut(i, (int16_t)((int32_t)vel << 4)); // 0..2032
		}
	}

	// Pulse outs
	for (int i = 0; i < 2; i++) {
		bool useRemote = (cfg_.out_src_bits & (i == 0 ? kOutPulse1 : kOutPulse2)) &&
		                 remoteFresh[i];
		bool level;
		if (useRemote)
			level = remotePulse_[i];
		else if (cfg_.engine_mode == 0)
			level = (i == 0) ? (leadGate_ > 0) : (bassGate_ > 0);
		else
			level = pulseTimer_[i] > 0;
		PulseOut(i, level);
	}
}

void CoWorkCard::ForwardInputs()
{
	// Pulse edges -> link notes, immediately
	for (int i = 0; i < 2; i++) {
		bool p = PulseIn(i);
		if (p != lastPulseIn_[i]) {
			lastPulseIn_[i] = p;
			if (cfg_.fwd_enable & (i == 0 ? kOutPulse1 : kOutPulse2)) {
				uint8_t note = (i == 0) ? kNotePulse1 : kNotePulse2;
				uint8_t status = (uint8_t)((p ? 0x90 : 0x80) | kLinkChannel);
				PushMidiOut(status, note, p ? 127 : 0, 3);
			}
		}
	}

	// CV mailboxes at 1 kHz; core 1 rate-limits the actual sends
	if (++cvDecim_ >= 48) {
		cvDecim_ = 0;
		gShared.cvFwd[0] = CVIn1();
		gShared.cvFwd[1] = CVIn2();
	}
}

void CoWorkCard::UpdateLeds()
{
	// One LED per sample (resonator round-robin) keeps the cost tiny.
	uint32_t phase = sampleCounter_ % 6;
	uint32_t halfSec = (sampleCounter_ / 24000) & 1;   // 1 Hz blink
	uint32_t eighth = (sampleCounter_ / 6000) & 1;     // 4 Hz blink
	bool conflict = nowUs_ < conflictUntilUs_;

	switch (phase) {
	case 0: { // Role
		bool on;
		if (conflict)
			on = ((sampleCounter_ / 3000) & 3) < 2 && eighth; // double-blink
		else if (role_ == kLeader)
			on = true;
		else if (clock_.HasClock() && !clock_.TimedOut(nowUs_))
			on = halfSec != 0;
		else
			on = eighth != 0;
		LedOn(0, on);
		break;
	}
	case 1: // Link / upload
		if (gShared.uploadActive)
			LedOn(1, ((sampleCounter_ / 2000) & 1) != 0);
		else
			LedOn(1, gShared.peerConnected != 0);
		break;
	case 2: // Engine mode
		LedOn(2, cfg_.engine_mode == 1);
		break;
	case 3: // Transport
		LedOn(3, running_);
		break;
	case 4: // Beat
		LedBrightness(4, (uint16_t)(beatLevel_ < 0 ? 0 : beatLevel_));
		break;
	case 5: // Activity + CPU overrun latch
		if (meter_.Overrun())
			LedOn(5, true);
		else
			LedBrightness(5, (uint16_t)(engine_.ActiveVoices() * 512));
		break;
	}
	if (beatLevel_ > 0) beatLevel_ -= 8;
}

// ===================== main callback =====================

void CoWorkCard::ProcessSample()
{
	meter_.BeginSample();
	sampleCounter_++;
	nowUs_ = time_us_32();

	// Boot settle: let the ADC mux deliver real switch/knob values first
	if (sampleCounter_ < kBootSettleSamples) {
		AudioOut1(0);
		AudioOut2(0);
		meter_.EndSample();
		return;
	}
	if (!roleLatched_) LatchRoleAtBoot();

	if (HandleQuiesce()) {
		meter_.EndSample();
		return;
	}

	AdoptBankIfChanged();
	AdoptConfigIfChanged();
	ConsumeMidiIn();
	HandleTransportRequests();
	HandleSwitch();
	HandleKnobs();
	AdvanceSequencer();
	RenderAndOutput();
	ForwardInputs();
	UpdateLeds();

	// Status snapshot for the CDC side
	gShared.statusTick = seq_.Tick();
	if (role_ == kLeader)
		gShared.statusBpmX10 = (uint32_t)(6000000000ull / tempoUspq_ / 10ull);
	else if (clock_.HasClock())
		gShared.statusBpmX10 = clock_.BpmX10();
	else
		gShared.statusBpmX10 = cfg_.tempo_bpm_x10;

	meter_.EndSample();
}

} // namespace cowork
