# CoWork — implementation notes for future maintainers (and AIs)

Read `docs/FORMATS.md` first: it is the contract between `src/` and
`web/cowork.html`. Change it only in lockstep with both sides.

## Architecture invariants

- **Core 0** runs `ProcessSample()` at 48 kHz (~20 µs budget at 192 MHz).
  It never writes flash and never blocks. All audio-path math is int32
  fixed point; `-Wdouble-promotion -Wfloat-conversion` enforce this.
- **Core 1** owns TinyUSB and every `flash_range_erase/program` call.
- **No `multicore_lockout` anywhere.** Flash writes are gated by the
  quiesce handshake in `SharedState.h` (`flashMutateReq`/`flashQuiesced`):
  core 0 kills voices and stops reading XIP before core 1 touches flash.
  Rationale: even with `copy_to_ram`, erase/program stalls XIP *data*
  reads (sample playback), and lockout has a known FIFO deadlock class
  (see releases/82_Computer_Grids/context.md).
- Leader/follower (Z switch at boot) is independent of USB host/device
  (CC pins, read via `USBPowerState()` on core 1 after a 150 ms settle).
  All four combinations share one code path through `MidiLink`.
- The follower's clock DLL (`seq/ClockRecovery.h`) clamps rate trim to
  ±3% and hard-resyncs beyond one beat. The leader never listens to its
  own clock; its timebase is the 48 kHz sample counter.
- The Main-knob loop-roll is an overlay in `CoWorkCard`: the sequencer's
  dispatch position wraps a window while `masterPhaseQ16_` keeps running.
  The leader's 0xF8 output and the follower's DLL both track the MASTER
  phase during a roll — never the rolled position — so the link stays
  steady and disengaging drops back in on time.

## Known traps

- `tusb_config.h`: do NOT `#define CFG_TUH_MIDI 1` — the vendored
  rppicomidi driver registers as an app driver; usbh.c breaks the build.
- If host-mode enumeration fails, try `COWORK_HOST_USE_TUSB_INIT=1`
  (drumdrum used `tusb_init()` instead of `tuh_init()` with the
  dual-role config; the ComputerCard example uses `tuh_init()`).
- The USB power circuit may only settle on a cold power-up; an RP2040
  reset alone can leave DFP/UFP stale (midi_device_host example comment).
- JEDEC flash detect must run single-core, before core 1 launches.
- Sequence/sample data is validated on parse (`ValidateSeqSlot`,
  `ValidateBankHeader`, `SlotIsPopulated`) — firmware never trusts flash.

## Host-side testing

- `sim/` builds the pure modules (engine, sequencer, clock DLL, flash
  map, parsers) with a desktop compiler: `cd sim && ./build.sh`.
- `web/test/run_tests.mjs` tests the web side including a MockDevice
  protocol implementation with randomized packet fragmentation.
