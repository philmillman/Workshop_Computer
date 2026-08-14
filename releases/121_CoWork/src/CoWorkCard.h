// CoWorkCard: ties the sequencer, engine, clock and link together inside
// the 48 kHz ProcessSample() callback (core 0). See docs/FORMATS.md and
// README.md for the behavioral spec.

#ifndef COWORK_CARD_H
#define COWORK_CARD_H

// The header-only ComputerCard implementation lives in ComputerCardImpl.cpp;
// every other translation unit gets declarations only.
#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"
#include "SharedState.h"
#include "engine/Engine.h"
#include "seq/Sequencer.h"
#include "seq/ClockRecovery.h"
#include "storage/Banks.h"
#include "dsp/cpu_meter.h"

namespace cowork {

class CoWorkCard : public ComputerCard, public Sequencer::Listener {
public:
	CoWorkCard() : meter_(20) {}

	virtual void ProcessSample() override;

	// USBPowerState() is protected in ComputerCard; core 1 needs it for
	// the boot-time role decision.
	USBPowerState_t PowerState() { return USBPowerState(); }

	// Sequencer::Listener
	void OnNoteOn(uint8_t part, uint8_t note, uint8_t vel) override;
	void OnNoteOff(uint8_t part, uint8_t note) override;
	void OnTempo(uint32_t uspq) override;
	void OnLoopWrap() override;

private:
	static constexpr uint32_t kBootSettleSamples = 480;   // 10 ms
	static constexpr uint32_t kLongPressSamples = 48000;  // 1 s
	static constexpr uint32_t kDoubleTapSamples = 16800;  // 350 ms window
	static constexpr uint32_t kDrumPulseSamples = 480;    // 10 ms triggers
	static constexpr int32_t kKnobPickup = 100;           // counts

	enum Role { kLeader = 0, kFollower = 1 };

	// -- helpers ------------------------------------------------------
	void LatchRoleAtBoot();
	bool HandleQuiesce();          // true = stay silent this sample
	void AdoptBankIfChanged();
	void AdoptConfigIfChanged();
	void ConsumeMidiIn();
	void HandleTransportRequests();
	void HandleSwitch();
	void HandleKnobs();
	void AdvanceSequencer();
	void RenderAndOutput();
	void ForwardInputs();
	void UpdateLeds();

	void StartTransport(bool resetPosition);
	void StopTransport();
	void AttachSong(uint32_t slot);
	void AdvanceSong();            // double-tap: next loaded slot
	void ApplyEngineMode(uint8_t mode);
	void ClearGates();
	void TransportTap();           // single-tap action
	uint32_t LoopZoneTicks(int zone) const;

	void PushMidiOut(uint8_t status, uint8_t d1, uint8_t d2, uint8_t len)
	{
		MidiOutEvent ev{ status, d1, d2, len };
		gShared.midiOut.Push(ev);
	}

	// -- modules ------------------------------------------------------
	Engine engine_;
	Sequencer seq_;
	ClockRecovery clock_;
	xmod::CpuMeter meter_;
	Config cfg_{};

	// -- boot / role --------------------------------------------------
	uint32_t sampleCounter_ = 0;
	bool roleLatched_ = false;
	Role role_ = kLeader;

	// -- bank / config adoption ---------------------------------------
	uint32_t lastBankSeq_ = 0xFFFFFFFF;
	uint32_t lastCfgSeq_ = 0;
	const BankInfo *bank_ = nullptr;

	// -- transport / tempo --------------------------------------------
	bool running_ = false;
	bool internalRun_ = false;     // follower running on internal clock
	uint32_t tempoUspq_ = 500000;  // current file/live tempo (leader)
	bool tempoKnobPicked_ = false;
	bool volKnobPicked_ = false;   // Y knob (volume)
	int32_t bootKnobX_ = -1;
	int32_t bootKnobY_ = -1;
	uint32_t lastStopUs_ = 0;      // DAW loop-wrap debounce
	uint8_t pendingSong_ = 0xFF;
	uint32_t lastClockTick_ = 0xFFFFFFFF; // leader 0xF8 dedup (incl. tick 0)

	// -- loop-roll (Main knob) ----------------------------------------
	// While engaged, the dispatch position loops a window while
	// masterPhaseQ16_ keeps running underneath (it also drives the
	// leader's clock output so the link stays steady).
	int loopZone_ = -1;            // 0..5 = lengths, 6 = off
	uint32_t loopLenTicks_ = 0;    // 0 = off
	uint32_t loopAnchor_ = 0;      // window start tick
	uint64_t masterPhaseQ16_ = 0;

	// -- switch -------------------------------------------------------
	uint32_t downCount_ = 0;
	bool longPressHandled_ = false;
	bool downArmed_ = false;       // ignore a switch held from boot
	bool wasDown_ = false;
	bool consumedAsDouble_ = false;
	uint32_t tapWindow_ = 0;       // pending single-tap countdown

	// -- gates / CV state ---------------------------------------------
	int leadGate_ = 0;
	int bassGate_ = 0;
	uint8_t lastLeadNote_ = 60;
	uint8_t lastBassNote_ = 36;
	uint32_t pulseTimer_[2] = { 0, 0 }; // percussive trigger outs
	uint8_t accentVel_ = 0;
	uint8_t cv2LaneVel_ = 0;

	// -- remote (peer-forwarded) values -------------------------------
	int32_t remoteCv_[2] = { 0, 0 };
	uint16_t remoteCvLsb_[2] = { 0, 0 };
	bool remotePulse_[2] = { false, false };
	uint32_t remoteCvAtUs_[2] = { 0, 0 };
	uint32_t remotePulseAtUs_[2] = { 0, 0 };

	// -- input forwarding ---------------------------------------------
	uint32_t cvDecim_ = 0;
	bool lastPulseIn_[2] = { false, false };

	// -- misc ---------------------------------------------------------
	uint32_t conflictUntilUs_ = 0;
	int32_t beatLevel_ = 0;
	uint32_t nowUs_ = 0;
};

} // namespace cowork

#endif
