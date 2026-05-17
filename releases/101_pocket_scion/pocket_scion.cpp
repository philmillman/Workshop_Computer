/*
 * Pocket Scion Computer Card
 *
 * Connects a Music Thing Workshop System Computer to an Instruo Pocket Scion
 * via USB MIDI (host mode).
 *
 * Features:
 *  - USB MIDI host: receives Note On/Off and Pitch Bend from the Scion
 *  - 2-channel monophonic CV/Gate output (v/Oct + gate) for configurable
 *    MIDI channels A and B
 *  - Stereo spatial echo: 3-parallel-delay-line effect inspired by the
 *    Fairfield Circuitry Placeholder (EB topology).  Three taps at
 *    golden-ratio time relationships (T, 0.618T, 0.382T) are panned to
 *    create a spatial stereo image.
 *  - Clock input (Pulse In 1) locks primary delay time to incoming tempo
 *  - CV In 1 modulates delay time
 *  - MIDI channel selection stored in flash
 *
 * Hardware mapping:
 *  Audio In 1/2  : Scion left/right audio output
 *  Audio Out 1/2 : Processed spatial echo audio
 *  CV Out 1      : v/Oct pitch, MIDI channel A
 *  CV Out 2      : v/Oct pitch, MIDI channel B
 *  Pulse Out 1   : Gate, MIDI channel A
 *  Pulse Out 2   : Gate, MIDI channel B
 *  Pulse In 1    : External clock input
 *  CV In 1       : Delay time modulation (±50 ms)
 *  Main Knob     : Dry/wet mix
 *  X Knob        : Time (primary tap delay length, 0–250 ms)
 *  Y Knob        : Feedback/regeneration (0–95 %)
 *  Switch Up     : Wide stereo spread (tap 1 hard-L, tap 3 hard-R)
 *  Switch Middle : Medium stereo spread
 *  Switch Down   : Narrow / mono (all taps centred)
 *
 * MIDI channel selection UI:
 *  Hold switch Up for > 2 seconds   → enter "Select Channel A" mode
 *  Hold switch Down for > 2 seconds → enter "Select Channel B" mode
 *  In either select mode:
 *    Main knob scans channels 1–16; LEDs 0–3 show channel in 4-bit binary
 *    Flick switch to Middle → confirm selection and return to normal
 *
 * Board requirement: Rev1_1 or newer (required for automatic USB host mode).
 * On older boards, USB host mode is not available and LED 0 will indicate
 * this limitation.
 */

#include "ComputerCard.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "bsp/board.h"
#include "tusb.h"
#include "usb_midi_host.h"
#include "placeholder_spatial.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define MIDI_NOTE_OFF   0x80
#define MIDI_NOTE_ON    0x90
#define MIDI_PITCHBEND  0xE0

// Samples before a held switch position counts as a "long hold" (2 s at 48 kHz)
static constexpr uint32_t LONG_HOLD_SAMPLES = 96000;

// Flash sector used for settings (last 4 KB sector)
static constexpr uint32_t SETTINGS_FLASH_OFFSET =
    PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
static constexpr uint8_t SETTINGS_MAGIC = 0xA7;

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
static volatile uint8_t  g_midiChannelA   = 0;   // 0–15 (= MIDI ch 1–16)
static volatile uint8_t  g_midiChannelB   = 0;
static volatile bool     g_midiConnected  = false;
static volatile bool     g_midiActivity   = false; // set by Core 1, cleared by Core 0
static volatile bool     g_saveRequest    = false; // set by Core 0, handled by Core 1

// USB device address (used by mount/rx callbacks and USBCore)
static uint8_t g_midiDevAddr = 0;

// ---------------------------------------------------------------------------
// PocketScion class
// ---------------------------------------------------------------------------
class PocketScion : public ComputerCard
{
public:
    PocketScion()
    {
        fx_               = PlaceholderSpatial();
        uiState_          = UIState::NORMAL;
        switchHoldTimer_  = 0;
        tempChannelA_     = 0;
        tempChannelB_     = 0;
        clockPeriod_      = 24000; // default 2 Hz clock → 500 ms period
        lastClockSample_  = 0;
        sampleCount_      = 0;
        clockDetected_    = false;
        clockTimeout_     = 0;
        midiActivityTimer_ = 0;
        clockLedTimer_    = 0;
        cvSmoothed_       = 0;

        // Start Core 1 for USB MIDI host
        multicore_launch_core1(core1Entry);
    }

    // Load saved MIDI channel settings from flash.
    // Call before Run().
    void LoadSettings()
    {
        const uint8_t *data = reinterpret_cast<const uint8_t *>(
            XIP_BASE + SETTINGS_FLASH_OFFSET);
        if (data[0] == SETTINGS_MAGIC)
        {
            g_midiChannelA = data[1] & 0x0F;
            g_midiChannelB = data[2] & 0x0F;
        }
        // else leave defaults (channel 0 = MIDI ch 1)
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

            // Handle save request from Core 0 (flash write with audio glitch)
            if (g_saveRequest)
            {
                g_saveRequest = false;
                saveMidiChannels();
            }
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
        // 2. Read and smooth CV In 1 (~100 Hz IIR low-pass)
        // -----------------------------------------------------------------
        int32_t cvRaw = CVIn1();  // –2048..2047
        cvSmoothed_ = (CV_SMOOTH_COEFF * cvSmoothed_ + 16 * cvRaw) >> 7;

        // -----------------------------------------------------------------
        // 3. Compute FX parameters from knobs, switch and CV modulation
        // -----------------------------------------------------------------
        int32_t mainKnob = KnobVal(Main);   // 0–4095
        int32_t xKnob    = KnobVal(X);
        int32_t yKnob    = KnobVal(Y);
        Switch  sw       = SwitchVal();

        // Switch selects stereo spread of the 3 delay taps
        int32_t spread;
        switch (sw)
        {
        case Switch::Up:     spread = 4096; break;  // wide: tap1 L, tap3 R
        case Switch::Middle: spread = 2048; break;  // medium
        default:             spread = 0;    break;  // Down: mono (all centred)
        }

        // Primary tap time from X knob (1–12000 samples = ~0–250 ms)
        int32_t timeSamples = 1 + ((xKnob * (PlaceholderSpatial::TIME_MAX - 1)) >> 12);

        // Feedback from Y knob (0–4095 → 0–31130, ~95 % max)
        int32_t feedbackLevel = (yKnob * 31130) >> 12;

        // Dry/wet mix from Main knob
        int32_t wetLevel = mainKnob;          // 0–4095
        int32_t dryLevel = 4095 - mainKnob;

        // CV In 1 always trims delay time by ±50 ms (±2400 samples)
        timeSamples += (cvSmoothed_ * 2400) >> 11;
        if (timeSamples < 1) timeSamples = 1;
        if (timeSamples > PlaceholderSpatial::TIME_MAX)
            timeSamples = PlaceholderSpatial::TIME_MAX;

        // -----------------------------------------------------------------
        // 4. Clock input: detect rising edge on Pulse In 1, measure period
        // -----------------------------------------------------------------
        if (PulseIn1RisingEdge())
        {
            uint32_t period = sampleCount_ - lastClockSample_;
            lastClockSample_ = sampleCount_;

            // Accept periods between 480 samples (100 Hz) and 96000 (0.5 Hz)
            if (period >= 480 && period <= 96000)
            {
                // Divide by 4 to convert from pulse-per-beat to quarter notes
                clockPeriod_   = (period + 2) >> 2;
                clockDetected_ = true;
                clockTimeout_  = 0;
            }

            // Flash clock LED briefly
            clockLedTimer_ = 1000;
        }

        // Clock timeout: if no pulse for >2 s, clear clock lock
        if (clockDetected_)
        {
            ++clockTimeout_;
            if (clockTimeout_ > 96000)
            {
                clockDetected_ = false;
            }
        }

        // When clock is locked, override primary tap time from X knob
        if (clockDetected_)
        {
            timeSamples = (int32_t)clockPeriod_;
            if (timeSamples > PlaceholderSpatial::TIME_MAX)
                timeSamples = PlaceholderSpatial::TIME_MAX;
        }

        // -----------------------------------------------------------------
        // 5. Apply spatial effect: 3 parallel delay taps (Placeholder topology)
        // -----------------------------------------------------------------
        fx_.setTime(timeSamples);
        fx_.setFeedback(feedbackLevel);
        fx_.setSpread(spread);

        int32_t wetL, wetR;
        fx_.process(inL, inR, wetL, wetR);

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
        // 7. MIDI channel selection UI
        // -----------------------------------------------------------------
        updateUI();

        // -----------------------------------------------------------------
        // 8. LEDs
        // -----------------------------------------------------------------
        updateLEDs();
    }

private:
    // -------------------------------------------------------------------------
    // UI state machine for MIDI channel selection
    // -------------------------------------------------------------------------
    enum class UIState { NORMAL, SELECT_CHAN_A, SELECT_CHAN_B };

    void __not_in_flash_func(updateUI)()
    {
        Switch sw = SwitchVal();

        switch (uiState_)
        {
        // ------------------------------------------------------------------
        case UIState::NORMAL:
        {
            // Long-hold Up → select channel A
            if (sw == Switch::Up)
            {
                ++switchHoldTimer_;
                if (switchHoldTimer_ >= LONG_HOLD_SAMPLES)
                {
                    uiState_          = UIState::SELECT_CHAN_A;
                    switchHoldTimer_  = 0;
                    tempChannelA_     = g_midiChannelA;
                }
            }
            // Long-hold Down → select channel B
            else if (sw == Switch::Down)
            {
                ++switchHoldTimer_;
                if (switchHoldTimer_ >= LONG_HOLD_SAMPLES)
                {
                    uiState_          = UIState::SELECT_CHAN_B;
                    switchHoldTimer_  = 0;
                    tempChannelB_     = g_midiChannelB;
                }
            }
            else
            {
                switchHoldTimer_ = 0;
            }
            break;
        }

        // ------------------------------------------------------------------
        case UIState::SELECT_CHAN_A:
        {
            // Map main knob (0–4095) to channel 0–15
            tempChannelA_ = (uint8_t)(KnobVal(Main) >> 8);  // 0–15

            // Flick switch to Middle → confirm and save
            if (sw == Switch::Middle)
            {
                if (SwitchChanged())
                {
                    g_midiChannelA  = tempChannelA_;
                    g_saveRequest   = true;
                    uiState_        = UIState::NORMAL;
                    switchHoldTimer_ = 0;
                }
            }
            // Flick to Down → move directly to channel B selection
            else if (sw == Switch::Down && SwitchChanged())
            {
                g_midiChannelA  = tempChannelA_;
                uiState_        = UIState::SELECT_CHAN_B;
                tempChannelB_   = g_midiChannelB;
                switchHoldTimer_ = 0;
            }
            break;
        }

        // ------------------------------------------------------------------
        case UIState::SELECT_CHAN_B:
        {
            // Map main knob (0–4095) to channel 0–15
            tempChannelB_ = (uint8_t)(KnobVal(Main) >> 8);  // 0–15

            // Flick switch to Middle → confirm and save
            if (sw == Switch::Middle)
            {
                if (SwitchChanged())
                {
                    g_midiChannelB  = tempChannelB_;
                    g_saveRequest   = true;
                    uiState_        = UIState::NORMAL;
                    switchHoldTimer_ = 0;
                }
            }
            // Flick to Up → move back to channel A selection
            else if (sw == Switch::Up && SwitchChanged())
            {
                g_midiChannelB  = tempChannelB_;
                uiState_        = UIState::SELECT_CHAN_A;
                tempChannelA_   = g_midiChannelA;
                switchHoldTimer_ = 0;
            }
            break;
        }
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

        if (uiState_ != UIState::NORMAL)
        {
            // In channel select mode: show selected channel in 4-bit binary on
            // LEDs 0–3.  LED 4 and 5 show which channel (A or B) is being set.
            uint8_t ch = (uiState_ == UIState::SELECT_CHAN_A)
                         ? tempChannelA_ : tempChannelB_;
            LedOn(0, (ch >> 3) & 1);
            LedOn(1, (ch >> 2) & 1);
            LedOn(2, (ch >> 1) & 1);
            LedOn(3,  ch       & 1);
            LedOn(4, uiState_ == UIState::SELECT_CHAN_A);
            LedOn(5, uiState_ == UIState::SELECT_CHAN_B);
        }
        else
        {
            // LED 0: USB MIDI device connected
            LedOn(0, g_midiConnected);

            // LED 1: MIDI activity (brief flash)
            LedOn(1, midiActivityTimer_ > 0);

            // LED 2: Gate A (note held on channel A)
            LedOn(2, g_gateA);

            // LED 3: Gate B (note held on channel B)
            LedOn(3, g_gateB);

            // LED 4: Clock pulse flash
            LedOn(4, clockLedTimer_ > 0);

            // LED 5: FX CV activity (brightness proportional to CV In 1 level)
            {
                int32_t cv      = cvSmoothed_ + 2048;  // 0–4095
                int32_t bright  = (cv * cv) >> 12;     // 0–4095 (approximate)
                LedBrightness(5, (uint16_t)bright);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Static flash save helper — called from Core 1
    // -------------------------------------------------------------------------
    static void saveMidiChannels()
    {
        uint8_t data[FLASH_PAGE_SIZE];
        memset(data, 0xFF, sizeof(data));
        data[0] = SETTINGS_MAGIC;
        data[1] = g_midiChannelA & 0x0F;
        data[2] = g_midiChannelB & 0x0F;

        // Pause Core 0 and disable interrupts to safely write flash.
        // This causes a brief audio glitch (~5 ms) which is acceptable
        // since it is only triggered by deliberate user action.
        multicore_lockout_start_blocking();
        uint32_t irqs = save_and_disable_interrupts();

        flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
        flash_range_program(SETTINGS_FLASH_OFFSET, data, FLASH_PAGE_SIZE);

        restore_interrupts(irqs);
        multicore_lockout_end_blocking();
    }

    // -------------------------------------------------------------------------
    // Member data
    // -------------------------------------------------------------------------
    PlaceholderSpatial fx_;

    UIState  uiState_;
    uint32_t switchHoldTimer_;
    uint8_t  tempChannelA_;
    uint8_t  tempChannelB_;

    uint32_t clockPeriod_;       // samples per quarter note
    uint32_t lastClockSample_;
    uint32_t sampleCount_;
    bool     clockDetected_;
    uint32_t clockTimeout_;

    uint32_t midiActivityTimer_;
    uint32_t clockLedTimer_;
    int32_t  cvSmoothed_;        // smoothed CV In 1 value
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
                if (chan == g_midiChannelA)
                {
                    if (noteOn) { g_noteNumA = buf[1]; g_gateA = true; }
                    else        { g_gateA = false; }
                    g_midiActivity = true;
                }
                if (chan == g_midiChannelB)
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
                if (chan == g_midiChannelA) g_pitchBendA = bend;
                if (chan == g_midiChannelB) g_pitchBendB = bend;
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
// Global PocketScion instance (placed in BSS so delay buffers fit in RAM)
// ---------------------------------------------------------------------------
static PocketScion g_card;

int main()
{
    // 144 MHz: exact multiple of 48 kHz Nyquist frequency → alias-free ADC
    set_sys_clock_khz(144000, true);

    // Register Core 0 as a multicore-lockout victim so that Core 1 can safely
    // pause Core 0 when writing to flash.
    multicore_lockout_victim_init();

    // Load persisted MIDI channel settings from flash
    g_card.LoadSettings();

    // Run audio processing (blocking — never returns)
    g_card.Run();
}
