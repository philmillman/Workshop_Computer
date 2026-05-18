/*
 * PlantHolder — Computer Card
 *
 * A nod to the Pocket Scion's roots in plant biosonification and to the
 * Fairfield Circuitry Placeholder's Householder-reflection reverb topology.
 *
 * Connects a Workshop System Computer to an Instruo Pocket Scion via USB
 * MIDI host.
 *
 * Features:
 *  - USB MIDI host: receives Note On/Off and Pitch Bend from the Scion
 *  - 2-channel monophonic CV/Gate (v/Oct + gate) on MIDI channels 1 and 2
 *    (configure routing in the Scion companion app)
 *  - Stereo reverb: Placeholder (EB) feedback delay network
 *  - Pulse In 1 → MIDI clock forwarded to the Scion (24 PPQN)
 *  - CV In 1 → DECAY, CV In 2 → SIZE (pedal D / S jacks)
 *
 * Control pages (Placeholder EB panel mapping):
 *  Switch Up   : MIX (main), SIZE (x), RATIO (y)
 *  Switch Mid  : MIX (main), DECAY (x), TONE (y)
 *  Switch Down hold > 1 s (latch, LED 5): MIX, MOD DEPTH, MOD TYPE (x),
 *                MOD LPF cutoff (y) — toggle again to exit
 *
 * Hardware mapping:
 *  Audio In 1/2  : Scion left/right audio output
 *  Audio Out 1/2 : Processed reverb audio
 *  CV Out 1/2    : v/Oct + gate — MIDI channels 1 and 2
 *  Pulse In 1    : External clock → MIDI clock out to Scion
 *  CV In 1       : DECAY CV
 *  CV In 2       : SIZE CV
 *
 * Board requirement: Rev1_1 or newer (required for automatic USB host mode).
 * On older boards, USB host mode is not available and LED 0 will indicate
 * this limitation.
 */

#include "ComputerCard.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"
#include "usb_midi_host.h"
#include "placeholder_reverb.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define MIDI_NOTE_OFF   0x80
#define MIDI_NOTE_ON    0x90
#define MIDI_PITCHBEND  0xE0

// Latch MOD page: hold Down > 1 s toggles (48 kHz)
static constexpr uint32_t MOD_LATCH_HOLD_SAMPLES = 48000;

// CV/Gate outputs: MIDI channel 1 → CV A, MIDI channel 2 → CV B
static constexpr uint8_t kMidiChannelA = 0;
static constexpr uint8_t kMidiChannelB = 1;

// MIDI real-time messages forwarded to the Scion
static constexpr uint8_t MIDI_TIMING_CLOCK = 0xF8;
static constexpr uint8_t MIDI_START        = 0xFA;
static constexpr uint8_t MIDI_STOP         = 0xFC;

// Pitch-bend range: ±2 semitones.
// In millivolts: 1 semitone = 1000/12 mV ≈ 83 mV.
// Full bend (±8192 MIDI units) → ±166 mV.
// Scale factor: 166 / 8192 ≈ (83 * 2) / 8192
static constexpr int32_t BEND_MV_PER_UNIT_NUM = 166;
static constexpr int32_t BEND_MV_PER_UNIT_DEN = 8192;

// CV In smoothing: IIR coefficient ≈ (127/128) for ~100 Hz LPF at 48 kHz
static constexpr int32_t CV_SMOOTH_COEFF = 127;

// ---------------------------------------------------------------------------
// Shared volatile state (written by Core 1 USB task, read by Core 0 audio)
// ---------------------------------------------------------------------------
static volatile uint8_t  g_noteNumA       = 60;
static volatile uint8_t  g_noteNumB       = 60;
static volatile bool     g_gateA          = false;
static volatile bool     g_gateB          = false;
static volatile int16_t  g_pitchBendA     = 0;   // –8192 .. +8191
static volatile int16_t  g_pitchBendB     = 0;
static volatile bool     g_midiConnected  = false;
static volatile bool     g_midiActivity   = false; // set by Core 1, cleared by Core 0

// External clock → MIDI clock (Core 0 produces, Core 1 transmits)
static volatile bool     g_sendMidiStart  = false;
static volatile bool     g_sendMidiStop   = false;
static volatile uint32_t g_pendingMidiClock = 0;

// USB device address (used by mount/rx callbacks and USBCore)
static uint8_t g_midiDevAddr = 0;

static void processMidiClockOut();

// ---------------------------------------------------------------------------
// PlantHolder class
// ---------------------------------------------------------------------------
class PlantHolder : public ComputerCard
{
public:
    PlantHolder()
    {
        reverb_            = PlaceholderReverb();
        switchHoldTimer_   = 0;
        sampleCount_       = 0;
        lastClockSample_   = 0;
        quarterNoteSamples_ = 12000;
        clockAccumulator_  = 0;
        clockActive_       = false;
        clockTimeout_      = 0;
        midiClockRunning_  = false;
        midiActivityTimer_ = 0;
        clockLedTimer_     = 0;
        cv1Smoothed_       = 0;
        cv2Smoothed_       = 0;
        modPageLatched_   = false;
        modHoldArmed_     = false;
        sizeSamples_      = 4800;
        ratio_            = 0;
        decay_            = 20000;
        tone_             = 0;
        modDepth_         = 512;
        modTypeKnob_      = 2048;
        modLpKnob_        = 4095;
        mixLevel_         = 2048;

        // Start Core 1 for USB MIDI host
        multicore_launch_core1(core1Entry);
    }

    // --------------------------------------------------------------------------
    // Boilerplate for Core 1 entry
    // --------------------------------------------------------------------------
    static void core1Entry()
    {
        // Wait 150 ms for USB power state to settle before initialising TinyUSB
        sleep_us(150000);
        board_init();
        tuh_init(TUH_OPT_RHPORT);

        while (true)
        {
            tuh_task();
            processMidiClockOut();
        }
    }

protected:
    // -------------------------------------------------------------------------
    // 48 kHz audio processing — runs on Core 0 under DMA interrupt
    // -------------------------------------------------------------------------
    virtual void __not_in_flash_func(ProcessSample)() override
    {
        ++sampleCount_;

        // -----------------------------------------------------------------
        // 1. Read audio inputs from Scion
        // -----------------------------------------------------------------
        int32_t inL = AudioIn1();
        int32_t inR = AudioIn2();

        // -----------------------------------------------------------------
        // 2. Smooth CV inputs (~100 Hz IIR)
        // -----------------------------------------------------------------
        cv1Smoothed_ = (CV_SMOOTH_COEFF * cv1Smoothed_ + 16 * CVIn1()) >> 7;
        cv2Smoothed_ = (CV_SMOOTH_COEFF * cv2Smoothed_ + 16 * CVIn2()) >> 7;

        // -----------------------------------------------------------------
        // 3. Knobs → FX parameters (page depends on switch / MOD latch)
        // -----------------------------------------------------------------
        int32_t mainKnob = KnobVal(Main);
        int32_t xKnob    = KnobVal(X);
        int32_t yKnob    = KnobVal(Y);
        Switch  sw       = SwitchVal();

        if (modPageLatched_)
        {
            modDepth_    = mainKnob;
            modTypeKnob_ = xKnob;
            modLpKnob_   = yKnob;
        }
        else
        {
            mixLevel_ = mainKnob;
            if (sw == Switch::Up)
            {
                int32_t span = PlaceholderReverb::TIME_MAX
                             - PlaceholderReverb::TIME_MIN;
                sizeSamples_ = PlaceholderReverb::TIME_MIN
                             + ((xKnob * span) >> 12);
                ratio_ = (yKnob * 2) - 4096;
            }
            else
            {
                decay_ = (xKnob * 31130) >> 12;
                tone_  = (yKnob * 2) - 4096;
            }
        }

        int32_t wetLevel = mixLevel_;
        int32_t dryLevel = 4095 - mixLevel_;

        // CV In 2 → SIZE (0–5 V unipolar-style: add up to full span)
        int32_t timeSamples = sizeSamples_ + ((cv2Smoothed_ + 2048) * 3) >> 2;
        if (timeSamples < PlaceholderReverb::TIME_MIN)
            timeSamples = PlaceholderReverb::TIME_MIN;
        if (timeSamples > PlaceholderReverb::TIME_MAX)
            timeSamples = PlaceholderReverb::TIME_MAX;

        // CV In 1 → DECAY (bipolar trim, pedal D useful range ±5 V)
        int32_t decayLevel = decay_ + ((cv1Smoothed_ * 31130) >> 12);
        if (decayLevel < 0) decayLevel = 0;
        if (decayLevel > 31130) decayLevel = 31130;

        // -----------------------------------------------------------------
        // 4. External clock → MIDI clock to Scion (does not affect reverb)
        // -----------------------------------------------------------------
        serviceExternalClock();

        // -----------------------------------------------------------------
        // 5. Placeholder EB feedback delay network
        // -----------------------------------------------------------------
        reverb_.setTime(timeSamples);
        reverb_.setRatio(ratio_);
        reverb_.setFeedback(decayLevel);
        reverb_.setTone(tone_);
        reverb_.setModDepth(modDepth_);
        reverb_.setModType(modTypeKnob_);
        reverb_.setModLpCutoff(modLpKnob_);

        int32_t wetL, wetR;
        reverb_.process(inL, inR, wetL, wetR);

        // Final dry/wet mix (Q12 scale factors)
        int32_t outL = ((inL * dryLevel) + (wetL * wetLevel)) >> 12;
        int32_t outR = ((inR * dryLevel) + (wetR * wetLevel)) >> 12;

        AudioOut1((int16_t)outL);
        AudioOut2((int16_t)outR);

        // -----------------------------------------------------------------
        // 6. CV / Gate outputs from MIDI state
        // -----------------------------------------------------------------
        {
            // Convert MIDI note + pitch bend to millivolts (1V/oct, A4=0V)
            // millivolts = (noteNum - 69) * 1000/12 + bend_mV
            int32_t mvA = ((int32_t)(g_noteNumA - 69) * 1000) / 12
                          + ((int32_t)g_pitchBendA * BEND_MV_PER_UNIT_NUM)
                            / BEND_MV_PER_UNIT_DEN;
            int32_t mvB = ((int32_t)(g_noteNumB - 69) * 1000) / 12
                          + ((int32_t)g_pitchBendB * BEND_MV_PER_UNIT_NUM)
                            / BEND_MV_PER_UNIT_DEN;

            CVOut1Millivolts(mvA);
            CVOut2Millivolts(mvB);
            PulseOut1(g_gateA);
            PulseOut2(g_gateB);
        }

        // -----------------------------------------------------------------
        // 7. MOD page latch (hold switch Down > 1 s)
        // -----------------------------------------------------------------
        updateModPageLatch();

        // -----------------------------------------------------------------
        // 8. LEDs
        // -----------------------------------------------------------------
        updateLEDs();
    }

private:
    void __not_in_flash_func(serviceExternalClock)()
    {
        if (PulseIn1RisingEdge())
        {
            uint32_t period = sampleCount_ - lastClockSample_;
            lastClockSample_ = sampleCount_;

            // 480–96000 samples ≈ 100 Hz–0.5 Hz between pulses
            if (period >= 480 && period <= 96000)
            {
                // One pulse per quarter note (same as Turing Machine tap-tempo / clock in)
                quarterNoteSamples_ = period;
                clockActive_        = true;
                clockTimeout_       = 0;
                clockAccumulator_   = 0;

                if (!midiClockRunning_)
                {
                    g_sendMidiStart = true;
                    midiClockRunning_ = true;
                }
            }

            clockLedTimer_ = 1000;
        }

        if (clockActive_)
        {
            ++clockTimeout_;
            if (clockTimeout_ > 96000)
            {
                clockActive_ = false;
                if (midiClockRunning_)
                {
                    g_sendMidiStop = true;
                    midiClockRunning_ = false;
                }
            }
        }

        if (clockActive_)
        {
            uint32_t samplesPerTick = quarterNoteSamples_ / 24;
            if (samplesPerTick < 20)
                samplesPerTick = 20;

            clockAccumulator_++;
            if (clockAccumulator_ >= samplesPerTick)
            {
                clockAccumulator_ -= samplesPerTick;
                if (g_pendingMidiClock < 96)
                    ++g_pendingMidiClock;
            }
        }
    }

    void __not_in_flash_func(updateModPageLatch)()
    {
        if (SwitchVal() == Switch::Down)
        {
            ++switchHoldTimer_;
            if (switchHoldTimer_ >= MOD_LATCH_HOLD_SAMPLES && !modHoldArmed_)
            {
                modPageLatched_ = !modPageLatched_;
                modHoldArmed_   = true;
            }
        }
        else
        {
            switchHoldTimer_ = 0;
            modHoldArmed_    = false;
        }
    }

    // -------------------------------------------------------------------------
    // LED indicators
    // -------------------------------------------------------------------------
    void __not_in_flash_func(updateLEDs)()
    {
        // Decrement activity timer
        if (midiActivityTimer_ > 0) --midiActivityTimer_;
        if (clockLedTimer_ > 0)     --clockLedTimer_;

        // Latch activity signal from Core 1
        if (g_midiActivity)
        {
            g_midiActivity    = false;
            midiActivityTimer_ = 3000;
        }

        LedOn(0, g_midiConnected);
        LedOn(1, midiActivityTimer_ > 0);
        LedOn(2, g_gateA);
        LedOn(3, g_gateB);
        LedOn(4, clockLedTimer_ > 0 || midiClockRunning_);
        LedOn(5, modPageLatched_);
    }

    // -------------------------------------------------------------------------
    // Member data
    // -------------------------------------------------------------------------
    PlaceholderReverb reverb_;

    uint32_t switchHoldTimer_;

    uint32_t sampleCount_;
    uint32_t lastClockSample_;
    uint32_t quarterNoteSamples_;
    uint32_t clockAccumulator_;
    bool     clockActive_;
    uint32_t clockTimeout_;
    bool     midiClockRunning_;

    uint32_t midiActivityTimer_;
    uint32_t clockLedTimer_;
    int32_t  cv1Smoothed_;
    int32_t  cv2Smoothed_;

    bool     modPageLatched_;
    bool     modHoldArmed_;

    int32_t  sizeSamples_;
    int32_t  ratio_;
    int32_t  decay_;
    int32_t  tone_;
    int32_t  modDepth_;
    int32_t  modTypeKnob_;
    int32_t  modLpKnob_;
    int32_t  mixLevel_;
};

// ---------------------------------------------------------------------------
// USB MIDI host callbacks (must be at C linkage / global scope)
// ---------------------------------------------------------------------------

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
                       uint8_t num_cables_rx, uint16_t num_cables_tx)
{
    (void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx;
    if (g_midiDevAddr == 0)
    {
        g_midiDevAddr    = dev_addr;
        g_midiConnected  = true;
    }
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)instance;
    if (dev_addr == g_midiDevAddr)
    {
        g_midiDevAddr   = 0;
        g_midiConnected = false;
        g_gateA = g_gateB = false;
    }
}

// ---------------------------------------------------------------------------
// MIDI byte parser — called from tuh_midi_rx_cb on Core 1
// ---------------------------------------------------------------------------
static void parseMidiByte(uint8_t b)
{
    static uint8_t runningStatus = 0;
    static uint8_t buf[3];
    static int     bufPos = 0;

    if (b & 0x80)
    {
        // Real-time messages (0xF8–0xFF) are single-byte; ignore here
        if (b >= 0xF8) return;
        // System common — reset running status
        if (b >= 0xF0) { runningStatus = 0; bufPos = 0; return; }

        // New status byte
        runningStatus = b;
        buf[0]  = b;
        bufPos  = 1;
    }
    else
    {
        if (runningStatus == 0) return;

        if (bufPos == 0)
        {
            buf[0]  = runningStatus;
            bufPos  = 1;
        }
        buf[bufPos++] = b;

        // Determine expected message length
        uint8_t type = runningStatus & 0xF0;
        int expected = 0;
        if (type == 0x80 || type == 0x90 || type == 0xA0 ||
            type == 0xB0 || type == 0xE0)
        {
            expected = 3;
        }
        else if (type == 0xC0 || type == 0xD0)
        {
            expected = 2;
        }

        if (bufPos >= expected && expected > 0)
        {
            uint8_t chan = runningStatus & 0x0F;
            bool noteOn  = (type == MIDI_NOTE_ON)  && (buf[2] > 0);
            bool noteOff = (type == MIDI_NOTE_OFF) ||
                           (type == MIDI_NOTE_ON && buf[2] == 0);

            if (noteOn || noteOff)
            {
                if (chan == kMidiChannelA)
                {
                    if (noteOn) { g_noteNumA = buf[1]; g_gateA = true; }
                    else        { g_gateA = false; }
                    g_midiActivity = true;
                }
                if (chan == kMidiChannelB)
                {
                    if (noteOn) { g_noteNumB = buf[1]; g_gateB = true; }
                    else        { g_gateB = false; }
                    g_midiActivity = true;
                }
            }
            else if (type == MIDI_PITCHBEND)
            {
                // 14-bit value centred at 8192
                int16_t bend = (int16_t)((buf[1] | ((uint16_t)buf[2] << 7)) - 8192);
                if (chan == kMidiChannelA) g_pitchBendA = bend;
                if (chan == kMidiChannelB) g_pitchBendB = bend;
            }

            // Ready for next message; keep running status
            bufPos = 1;
        }
    }
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
    if (dev_addr != g_midiDevAddr || num_packets == 0) return;

    uint8_t  cableNum;
    uint8_t  buffer[512];
    uint32_t bytesRead;

    while ((bytesRead = tuh_midi_stream_read(dev_addr, &cableNum,
                                             buffer, sizeof(buffer))) > 0)
    {
        for (uint32_t i = 0; i < bytesRead; ++i)
            parseMidiByte(buffer[i]);
    }
}

void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

// ---------------------------------------------------------------------------
// Forward Pulse In 1 as MIDI clock (and start/stop) to the Scion
// ---------------------------------------------------------------------------
static void processMidiClockOut()
{
    if (g_midiDevAddr == 0 || !tuh_midi_configured(g_midiDevAddr))
        return;

    if (tuh_midih_get_num_tx_cables(g_midiDevAddr) < 1)
        return;

    const uint8_t cable = 0;
    bool flushed = false;

    if (g_sendMidiStart)
    {
        g_sendMidiStart = false;
        uint8_t msg = MIDI_START;
        tuh_midi_stream_write(g_midiDevAddr, cable, &msg, 1);
        flushed = true;
    }

    if (g_sendMidiStop)
    {
        g_sendMidiStop = false;
        uint8_t msg = MIDI_STOP;
        tuh_midi_stream_write(g_midiDevAddr, cable, &msg, 1);
        flushed = true;
    }

    uint32_t ticks = g_pendingMidiClock;
    if (ticks > 24)
        ticks = 24;
    g_pendingMidiClock -= ticks;

    uint8_t clk = MIDI_TIMING_CLOCK;
    for (uint32_t i = 0; i < ticks; ++i)
    {
        tuh_midi_stream_write(g_midiDevAddr, cable, &clk, 1);
        flushed = true;
    }

    if (flushed)
        tuh_midi_stream_flush(g_midiDevAddr);
}

// ---------------------------------------------------------------------------
// Global PlantHolder instance (placed in BSS so reverb buffers fit in RAM)
// ---------------------------------------------------------------------------
static PlantHolder g_card;

int main()
{
    // 144 MHz: exact multiple of 48 kHz Nyquist frequency → alias-free ADC
    set_sys_clock_khz(144000, true);

    g_card.Run();
}
