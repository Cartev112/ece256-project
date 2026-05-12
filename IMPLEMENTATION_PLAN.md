# TM4C Music Player With Synchronized LED Activity

## 1. Project Goal

Build a simple embedded music player on the TM4C LaunchPad that plays a short song while driving the onboard RGB LED in sync with musical phrases. The user controls playback with `SW1`.

The baseline demonstration for Milestone 3 is:

- `Twinkle Twinkle Little Star` plays correctly.
- The onboard RGB LED changes color once per phrase.
- `SW1` starts playback from idle and pauses/resumes during playback.
- The firmware uses an FSM with at least `IDLE`, `PLAYING`, and `PAUSED`.
- The project compiles without warnings and includes clear comments.

The design should leave room to integrate `UART` in Milestone 4 and `uDMA` in Milestone 5.

## 2. Platform Assumptions

- Board: `EK-TM4C123GXL` / `TM4C123GH6PM`
- Toolchain: `Code Composer Studio` with TI TivaWare driverlib
- System clock target: `40 MHz` or `50 MHz`
- Onboard LEDs:
  - `PF1` = red
  - `PF2` = blue
  - `PF3` = green
- User switch:
  - `PF4` = `SW1`, active low
- Audio output choice for Milestone 3:
  - Primary path: `PWM` output, low-pass filtered into a speaker/amplifier or buzzer-compatible input
  - Backup path: simple `R-2R DAC` on a GPIO port if PWM audio quality or hardware is not sufficient

## 3. Functional Requirements

### Milestone 2: FSM Design

- Define the playback states, inputs, transitions, outputs, and initial state.
- Represent the FSM explicitly in code with an enum and transition logic.
- Separate event detection from output actions.

### Milestone 3: Audio + LED Demo

- Play `Twinkle Twinkle Little Star`.
- Change RGB LED color at each phrase boundary.
- Use `SW1` to start, pause, and resume playback.
- Keep timing stable enough that the tune is recognizable.
- Build warning-free.

### Milestone 4: UART Integration

- Add serial debug/status output.
- Optionally allow simple runtime commands such as `play`, `pause`, `resume`, `stop`, or song select.
- Keep button control working even after UART is added.

### Milestone 5: DMA Integration (Due May 5, 2026)

- Move repetitive audio sample updates to `uDMA`.
- Reduce CPU involvement in audio streaming.
- Preserve correct note timing and LED phrase synchronization.

## 4. Proposed System Architecture

Split the project into small modules even if they initially live in one file.

- `blinky.c`
  - system startup
  - scheduler loop
  - top-level FSM dispatch
- `fsm.[ch]`
  - state enum
  - event enum
  - transition logic
- `music.[ch]`
  - note definitions
  - song table
  - phrase markers
  - playback timing
- `player_audio.[ch]`
  - PWM or DAC setup
  - note-to-frequency handling
  - sample/tone generation
- `led.[ch]`
  - RGB initialization
  - phrase-based color updates
- `button.[ch]`
  - `SW1` GPIO interrupt setup
  - SysTick-confirmed debounce
  - press event generation
- `uart.[ch]`
  - UART0 init
  - blocking transmit helpers
  - interrupt-driven RX ring buffer
- `commands.[ch]`
  - UART command decoding
  - command-to-FSM event mapping
- `systick.[ch]`
  - 5 ms scheduler tick
  - SysTick interrupt handler
- `board.[ch]` / `board_pins.h`
  - GPIO, PWM, and pin mux initialization
- `dma.[ch]` for Milestone 5
  - `uDMA` channel config
  - ping-pong or circular buffer support

The current repo is now split into these modules. The older teammate prototype `audio.c` remains standalone and is excluded from the managed project build because it has its own `main()`.

## 5. FSM Design

### States

- `IDLE`
  - playback stopped
  - current song index reset to start
  - LED shows idle color
- `LOAD_PHRASE`
  - selects the current phrase's LED pattern
  - prepares phrase-level bookkeeping before the next note is loaded
- `LOAD_NOTE`
  - reads the next note from the song table
  - prepares frequency, duration, phrase ID, and rest/note status
- `NOTE_ON`
  - actively outputs the current note through PWM or DAC
  - holds until the note duration expires
- `NOTE_GAP`
  - inserts a short rest between notes
  - decides whether the next transition is another note, a new phrase, or song completion
- `PAUSED`
  - current playback position retained
  - audio output muted or disabled
  - LED shows paused color
- `SONG_COMPLETE`
  - song completed
  - audio output disabled
  - LED shows completion color
  - next button press restarts song
- `ERROR` (optional)
  - unrecoverable hardware or data problem
  - audio disabled
  - LED shows an error pattern

This still satisfies the required `IDLE`, `PLAYING`, and `PAUSED` behavior, but it expands "playing" into concrete sub-states that match how the embedded software will actually work.

For milestone grading, `LOAD_PHRASE`, `LOAD_NOTE`, `NOTE_ON`, and `NOTE_GAP` together represent the active playing region.

### Inputs / Events

- `EVT_SW1_PRESS`
  - debounced falling-edge press on `PF4`
- `EVT_PHRASE_READY`
  - phrase LED/output setup has completed
- `EVT_NOTE_READY`
  - note frequency/duration setup has completed
- `EVT_NOTE_DONE`
  - current note duration elapsed
- `EVT_GAP_DONE`
  - inter-note gap elapsed and the next note is in the same phrase
- `EVT_PHRASE_DONE`
  - inter-note gap elapsed and the next note starts a new phrase
- `EVT_SONG_DONE`
  - last note completed
- `EVT_UART_PLAY` (Milestone 4)
- `EVT_UART_PAUSE` (Milestone 4)
- `EVT_UART_STOP` (Milestone 4)
- `EVT_DMA_HALF` / `EVT_DMA_FULL` (Milestone 5, if ping-pong buffering is used)

### Outputs / Actions

- reset song index and phrase index
- load the current phrase color
- load the current note frequency and duration
- start audio generation for the current note
- stop audio for the inter-note gap
- mute audio while paused
- advance to the next note
- advance to the next phrase
- mark song completion
- send state/status text over UART
- refill the next DMA audio buffer

### Initial State

- Initial state: `IDLE`
- Initial outputs:
  - song index = `0`
  - phrase index = `0`
  - LED color = idle color, recommended `blue`
  - audio output disabled
- resume state = `IDLE`

### State Transition Summary

| Current State | Event | Next State | Action |
|---|---|---|---|
| `IDLE` | `EVT_SW1_PRESS` | `LOAD_PHRASE` | reset song position, reset phrase index |
| `LOAD_PHRASE` | `EVT_PHRASE_READY` | `LOAD_NOTE` | apply phrase LED color |
| `LOAD_NOTE` | `EVT_NOTE_READY` | `NOTE_ON` | load frequency and duration |
| `NOTE_ON` | `EVT_NOTE_DONE` | `NOTE_GAP` | stop or mute audio |
| `NOTE_GAP` | `EVT_GAP_DONE` | `LOAD_NOTE` | advance note index |
| `NOTE_GAP` | `EVT_PHRASE_DONE` | `LOAD_PHRASE` | advance note index and phrase index |
| `NOTE_GAP` | `EVT_SONG_DONE` | `SONG_COMPLETE` | stop audio and mark song complete |
| any active playback state | `EVT_SW1_PRESS` | `PAUSED` | save previous state and mute audio |
| `PAUSED` | `EVT_SW1_PRESS` | saved active state | resume previous playback sub-state |
| `SONG_COMPLETE` | `EVT_SW1_PRESS` | `LOAD_PHRASE` | restart from first note |

The code should store a `resume_state` so that pausing during `NOTE_ON`, `NOTE_GAP`, `LOAD_NOTE`, or `LOAD_PHRASE` resumes the correct sub-state instead of blindly returning to a generic playing state.

### Milestone 2 Board Demo Mapping

Until real audio timing is added, the board demo can use timed simulated events to walk through the active playback states:

- `IDLE`: blue
- `LOAD_PHRASE`: yellow
- `LOAD_NOTE`: cyan
- `NOTE_ON`: green
- `NOTE_GAP`: white
- `PAUSED`: red
- `SONG_COMPLETE`: magenta

This gives a visible way to verify that the expanded FSM is running even before PWM, note tables, and phrase timing are complete.

## 6. Song Representation

Use table-driven song data so the actual melody and phrase pattern can change later without rewriting the FSM.

Recommended structures:

```c
typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint8_t phrase_id;
} note_t;
```

```c
typedef struct {
    const note_t *notes;
    uint16_t note_count;
    uint8_t phrase_count;
} song_t;
```

Implementation notes:

- Use a rest as `frequency_hz = 0`.
- `phrase_id` lets LED logic change only when the phrase number changes.
- Start with one constant table for `Twinkle Twinkle Little Star`.
- Later songs should only require replacing the note table and phrase mapping.

## 7. Audio Implementation Plan

### Milestone 3 Audio Path

Use one of these approaches:

1. `PWM tone generation` for square-wave note playback
2. `R-2R DAC` for sample playback if external resistor ladder hardware is available

Recommended baseline: `PWM tone generation`, because it is the fastest path to a recognizable song.

Current implementation choice: `PB6 / M0PWM0` is the PWM audio output pin. It should feed a small speaker driver, powered buzzer input, or RC filter/amplifier. Do not connect a bare low-impedance speaker directly to the microcontroller pin.

#### PWM plan

- Configure `PB6` as `M0PWM0`.
- For each note:
  - compute load value from target note frequency
  - set a 50% duty cycle
  - enable PWM while note is active
- For rests:
  - disable PWM output or set duty cycle to zero
- Use interrupt-driven SysTick to measure note duration in `5 ms` ticks

This will not sound like a rich instrument, but it is enough for Milestone 3.

### Milestone 5 Audio Path With DMA

Current implementation: audio has been moved from note-level PWM frequency changes to timer-triggered `uDMA` sample streaming.

- `PB6 / M0PWM0` is configured as a fixed high-frequency PWM carrier.
- `Timer0A` runs at an `8000 Hz` audio sample rate.
- `uDMA` channel `18` moves the next sample into `PWM0 CMPA`.
- Primary and alternate buffers are used in ping-pong mode.
- The Timer0A ISR refills whichever buffer just completed.

This gives a real DMA workload: the CPU still chooses notes through the FSM, but DMA handles repetitive high-rate sample output.

## 8. LED Synchronization Plan

Phrase-level synchronization is enough for the stated milestone.

Recommended phrase colors for `Twinkle Twinkle`:

- Phrase 0: `RED`
- Phrase 1: `GREEN`
- Phrase 2: `BLUE`
- Phrase 3: `YELLOW` (`RED + GREEN`)
- Idle: `BLUE`
- Paused: `RED`
- Done: `GREEN`

Synchronization method:

- Each note carries a `phrase_id`.
- On each note advance, compare `phrase_id` to the prior note's `phrase_id`.
- If the phrase changed, update the LED color immediately before or at the same time as the next note starts.

This keeps LED timing tied to music data, not hard-coded delays.

## 9. Button Handling Plan

`SW1` on `PF4` must be debounced.

Recommended method:

- Use a falling-edge GPIO interrupt on `PF4` to detect the raw press edge.
- Mask the GPIO interrupt while the press is being debounced.
- Confirm the press with `3` consecutive stable samples from the `5 ms` SysTick-driven scheduler.
- Generate one FSM event after the stable press is confirmed.
- Wait for a stable release before re-enabling the GPIO interrupt.

This keeps the button interrupt short while still handling mechanical bounce deterministically.

## 10. UART Integration Plan

### Objectives for Milestone 4

- Print current state changes:
  - `"IDLE"`
  - `"PLAYING note=5 phrase=1"`
  - `"PAUSED"`
  - `"DONE"`
- Print button events and basic timing diagnostics.
- Optionally accept single-character commands:
  - `p` = play/resume
  - `a` = pause
  - `s` = stop
  - `r` = restart

### UART Hardware Plan

- Use `UART0` on `PA0/PA1`
- `115200-8-N-1`
- Current implementation: interrupt-driven UART RX with a small software ring buffer, blocking UART TX for short status messages, and interrupt-driven SysTick for the scheduler tick

### Integration Rule

UART must inject the same FSM events used by the button path. Do not create separate control logic for UART and `SW1`.

### Current UART Command Set

- `p` = play or resume
- `a` = pause
- `s` = stop and return to `IDLE`
- `r` = restart from the first note
- `h` or `?` = print help/status
- `d` = print DMA counters

## 11. DMA Integration Plan

### Objectives for Milestone 5

- Demonstrate that audio streaming uses `uDMA`.
- Keep the main loop free for FSM management, button handling, LED updates, and UART.
- Preserve the existing FSM behavior: `NOTE_ON` starts audio, `NOTE_GAP`/`PAUSED`/`IDLE` stop or mute audio, and SysTick still owns note-duration timing.

### Recommended DMA Direction

Use timer-triggered `uDMA` to update the PWM duty cycle on the existing `PB6 / M0PWM0` audio output. This is now the implemented audio backend.

Recommended signal path:

```text
Timer0A timeout at sample rate
    -> uDMA channel request
    -> copy next sample from audio buffer
    -> write sample to PWM0 generator 0 CMPA register
    -> PB6 PWM duty cycle changes
    -> RC filter / amplifier / powered speaker input turns PWM duty into audio level
```

Important distinction:

- The timer should trigger the DMA request.
- The DMA destination can be the PWM compare register.
- Do not assume GPIO or PWM will automatically trigger DMA requests by themselves.
- SysTick remains the note-duration scheduler; the DMA timer is only the audio sample clock.

### Why PWM-Duty DMA Instead of Note-Frequency DMA

The current Milestone 3 audio changes the PWM period once per note. That is not a strong DMA use case because the CPU only updates a register a few times per second.

For Milestone 5, make DMA move many samples per second. Instead of changing PWM frequency directly, configure PWM as a fixed high-frequency carrier and use DMA to update the duty cycle:

- PWM carrier: approximately `50 kHz` to `80 kHz`
- Current PWM carrier: approximately `62.5 kHz`
- Current audio sample rate: `8 kHz`
- Sample values: unsigned duty values, for example `0..255` if PWM load is `255`
- Current buffer type: `uint32_t`, because the DMA writes the 32-bit `PWM0 CMPA` register
- Destination: `PWM0_BASE + PWM_O_0_CMPA`

This makes DMA responsible for repetitive high-rate audio output while the CPU only refills buffers and handles the FSM.

### Current DMA Files

The DMA implementation lives in these files rather than being folded back into `blinky.c`:

- `dma.h`
  - public DMA audio API
  - status counters for testing
- `dma.c`
  - uDMA control table
  - ping-pong audio buffers
  - Timer0A sample-rate setup
  - uDMA channel setup
  - DMA/timer interrupt handlers
- `player_audio.c`
  - keeps the old `AudioStart()` / `AudioStop()` API stable
  - calls `DmaAudioStartNote()` and `DmaAudioStop()` internally
- `startup_ccs.c`
  - maps the Timer0A vector to `Timer0A_Handler`
  - maps the uDMA error vector to `uDMAError_Handler`

Current public API:

```c
void DmaAudioInit(void);
void DmaAudioStartNote(uint16_t frequency_hz);
void DmaAudioStop(void);
void DmaAudioPause(void);
void DmaAudioResume(void);
void DmaAudioGetStatus(dma_audio_status_t *status);
```

The existing `player_audio.h` API is intentionally still stable:

```c
void AudioStart(uint16_t frequency_hz);
void AudioStop(void);
```

That prevents the FSM from needing a rewrite. The FSM still calls `AudioStart()` and `AudioStop()` exactly as before.

### DMA Resource Choices

Use these initial choices unless there is a specific conflict:

- DMA request source: `Timer0A`
- DMA channel: Timer0A uDMA channel, commonly exposed by TivaWare as `UDMA_CHANNEL_TMR0A` / channel `18`
- Timer event: Timer A timeout DMA event
- Audio output: `PB6 / M0PWM0`
- DMA mode: ping-pong
- Buffers: two static `uint32_t[128]` buffers
- Sample rate: `8000 Hz`
- Buffer duration: `128 / 8000 = 16 ms` per buffer
- Interrupt handlers to add in `startup_ccs.c`:
  - `Timer0A_Handler` for Timer0A DMA-complete/refill work
  - `uDMAError_Handler` for bus/alignment/address errors

Keep every DMA buffer `static` or global. Do not allocate DMA buffers on the stack.

### Control Table Requirement

The uDMA control table must be aligned to a `1024-byte` boundary.

For CCS/TI compiler style, the current code uses:

```c
#pragma DATA_ALIGN(g_sDmaControlTable, 1024)
static dma_control_entry_t g_sDmaControlTable[64];
```

If the project is built with a clang/GCC-style compiler, the code uses an aligned attribute instead. Verify the map file or debugger address: the control table address must end in `0x000`, `0x400`, `0x800`, or `0xC00`.

Current build verification: `g_sDmaControlTable` appears in the map at `0x20000000`, which is correctly aligned.

### Buffer Fill Strategy

The first working version uses a simple square-wave duty pattern:

- maintain a phase accumulator per note
- for each sample:
  - advance a fixed-point phase accumulator
  - output a low or high compare value around the PWM midpoint
  - write the compare value into the DMA buffer

A sine table would sound better, but the square-wave buffer is enough to prove that DMA is moving audio-rate samples. Keep fixed-point integer math in the ISR; avoid floating point inside interrupt handlers.

Example buffer-fill behavior:

```text
fill_audio_buffer(buffer, count, note_frequency)
    for each sample:
        update phase
        compute duty sample
        buffer[i] = duty sample for PWM CMPA
```

When the note is a rest (`frequency_hz == 0`), the code stops DMA audio and mutes PWM instead of streaming a rest buffer.

### Ping-Pong Flow

The intended runtime flow is:

```text
DmaAudioStartNote()
    fill primary buffer
    fill alternate buffer
    configure primary uDMA descriptor
    configure alternate uDMA descriptor
    start Timer0A sample clock
    enable uDMA channel

Timer0A/uDMA interrupt
    identify which descriptor stopped
    refill only the stopped buffer
    re-arm that descriptor in ping-pong mode
    increment debug counters
```

The ISR must not:

- advance `song_index`
- change FSM state
- print UART text
- update LEDs
- spend a long time computing complex audio

The ISR may:

- clear the timer DMA interrupt flag
- check primary/alternate DMA mode
- refill the completed buffer
- re-arm the completed descriptor
- increment counters

### Integration With Current FSM

The current FSM should remain responsible for note timing.

Integration points:

- `STATE_NOTE_ON` entry:
  - `AudioStart(current_note.frequency_hz)`
  - internally starts or retunes DMA audio
- `STATE_NOTE_GAP` entry:
  - `AudioStop()`
  - stops Timer0A DMA requests and mutes PWM
- `STATE_PAUSED` entry:
  - stop Timer0A and disable DMA requests
  - preserve FSM `resume_state` and `resume_ticks` exactly as now
- resume from `PAUSED`:
  - if resuming into `NOTE_ON`, restart DMA for the current note
- `STATE_IDLE` and `STATE_SONG_COMPLETE`:
  - stop Timer0A, disable the uDMA channel, mute PWM

Do not let the DMA ISR decide when a note is done. The note is done only when the existing SysTick/FSM timing emits `EVT_NOTE_DONE`.

### Recommended Implementation Order

1. Add empty `dma.[ch]` with stub functions and build successfully.
2. Add the aligned uDMA control table and a `DmaAudioInit()` function that only enables the uDMA peripheral.
3. Add a memory-to-memory uDMA self-test if using driverlib examples as reference. Verify source and destination buffers match before touching audio.
4. Configure Timer0A as a sample-rate timer, but do not enable DMA yet. Toggle a debug counter or GPIO in the Timer0A ISR to verify the rate.
5. Configure one basic uDMA transfer from a test buffer to a harmless destination first, such as a spare debug variable.
6. Move to ping-pong mode with two buffers and verify primary/alternate completion counters increase evenly.
7. Change the DMA destination to `PWM0 CMPA` and stream a constant duty value. Verify PB6 PWM is still present and duty is stable.
8. Stream a simple repeating ramp or square-duty table. Verify PB6 duty changes at the sample rate on a scope or logic analyzer.
9. Connect `AudioStart()` / `AudioStop()` to the DMA path.
10. Re-test `SW1`, UART commands, pause/resume, phrase LEDs, and song completion.

Call `DmaAudioInit()` once during startup after `BoardInit()` and before playback begins. Do not start Timer0A or enable the DMA channel until `AudioStart()` is called for a real note.

### Driverlib vs Direct Register Note

The project currently uses direct register access to avoid the earlier prebuilt `driverlib.lib` toolchain mismatch. For DMA, there are two reasonable options:

- Use TivaWare `driverlib` APIs only if the project links cleanly with the installed compiler.
- Otherwise, use TivaWare headers and examples as reference, but configure uDMA with direct register writes like the rest of this project.

Do not spend hours debugging audio logic if the first problem is actually a toolchain/library mismatch. Confirm a clean build before testing DMA behavior.

### Useful Debug Counters

The DMA implementation exposes a status struct and prints it from UART when the user sends `d`, `h`, or `?`.

Current counters:

- `primary_done_count`
- `alternate_done_count`
- `dma_error_count`
- `timer_dma_irq_count`
- `buffer_refill_count`
- `underrun_count`
- `bad_isr_count`
- `active_frequency_hz`

Expected healthy behavior:

- primary and alternate counts both increase
- counts stay close to each other
- error count stays `0`
- underrun count stays `0`
- status prints still work while audio plays
- `active_frequency_hz` is nonzero during a note and zero when stopped

### Why This Matters

DMA should handle high-rate repetitive sample movement. The CPU should only:

- track song progress
- select the next note or waveform segment
- update phrase LEDs
- process user input and UART
- refill the completed DMA buffer before it is needed again

### Common Failure Modes

| Symptom | Likely Cause | What To Check |
|---|---|---|
| no DMA interrupts | wrong NVIC vector or channel not enabled | startup vector, NVIC enable, Timer0A DMA event enable |
| one buffer plays then silence | completed descriptor not re-armed | primary/alternate mode check and descriptor reload |
| hard fault or uDMA error | bad address or control table alignment | 1024-byte alignment, source/destination addresses, static buffers |
| distorted or unstable audio | sample rate or PWM carrier misconfigured | Timer0A load, PWM load, CMPA range |
| note timing changes | DMA ISR is controlling notes | keep note timing in SysTick/FSM only |
| pause does not stop sound | timer or DMA request still enabled | stop Timer0A, disable channel, mute PWM |
| UART becomes unreliable | ISR doing too much work | keep DMA ISR short; avoid UART prints inside ISR |

## 12. File and Code Milestones

### Milestone 2 Deliverables

- Written FSM diagram or transition table
- Enumerated state and event definitions
- Initial software architecture

### Milestone 3 Deliverables

- Playable `Twinkle Twinkle Little Star`
- Working RGB phrase changes
- Functional `SW1` start/pause/resume
- Warning-free build
- Code comments on hardware setup, timing, and FSM behavior

### Milestone 4 Deliverables

- UART status messages
- UART command parser
- FSM controlled by both button and UART events

### Milestone 5 Deliverables

- DMA-based audio streaming
- Buffer management logic
- Verification that LED sync and controls still work

## 13. Implementation Sequence

1. Clean up the current `blinky.c` template and replace LED blink logic with board init helpers.
2. Add GPIO init for `PF1`, `PF2`, `PF3`, and `PF4`.
3. Add a periodic tick source for debounce and note timing.
4. Define the FSM enums, transition function, and state variables.
5. Add table-driven `Twinkle Twinkle` note data with phrase IDs.
6. Implement simple PWM note playback.
7. Connect phrase changes to RGB LED color updates.
8. Add `SW1` debounce and map each valid press to FSM events.
9. Verify start, pause, resume, and end-of-song behavior.
10. Add UART status output and optional command input.
11. Add `dma.[ch]` stubs and keep the existing audio behavior unchanged.
12. Prove uDMA with a memory-to-memory or harmless test-pattern transfer.
13. Add Timer0A as the DMA sample-rate trigger.
14. Add ping-pong audio buffers and completion counters.
15. Stream test samples into `PWM0 CMPA` and verify PB6 with a scope or logic analyzer.
16. Connect `AudioStart()` / `AudioStop()` to the DMA-backed audio path.
17. Re-test timing, control response, UART commands, and code warnings after each milestone.

## 14. Test Plan

### Functional Tests

- On reset, system enters `IDLE`.
- First `SW1` press starts playback from the first note.
- Second `SW1` press pauses playback without losing position.
- Third `SW1` press resumes from the paused note.
- LED color changes only at phrase boundaries.
- Song completes and enters the done/idle end state correctly.
- `PB6 / M0PWM0` outputs the current note during `NOTE_ON`.
- PWM output is disabled during `NOTE_GAP`, `PAUSED`, `IDLE`, and `SONG_COMPLETE`.

### Timing Tests

- Verify note lengths are consistent by ear and with scope/logic analyzer if available.
- Verify phrase LED changes line up with intended musical sections.
- Verify debounce prevents double-triggering from one press.

### Integration Tests

- UART commands produce the same behavior as button events.
- DMA audio still honors pause/resume and song restart.
- Build completes with no warnings in Debug configuration.

### DMA-Specific Validation Tests

Run these in order. Do not skip directly to full-song playback.

1. Build-only test:
   - verify `dma.c` and `dma.h` are included in the project
   - build the project
   - expected result: compile and link complete without warnings

2. uDMA control-table test:
   - inspect `g_sDmaControlTable` in the debugger or map file
   - verify the address is `1024-byte` aligned
   - expected result: current map shows `0x20000000`

3. Timer sample-rate test:
   - flash the board
   - open UART at `115200-8-N-1`
   - send `p` to start playback
   - send `d` while a note is playing
   - expected result: `irq`, `refill`, `primary`, and `alternate` increase

4. Basic DMA movement test:
   - send `d` before playback starts
   - expected result: counters are stable and `freq=0`
   - send `p`
   - expected result: `freq` becomes the current note frequency

5. Ping-pong test:
   - print `primary_done_count` and `alternate_done_count`
   - expected result: both counters increase and stay close

6. PWM destination test:
   - probe `PB6` with a scope or logic analyzer while playback is active
   - expected result: a fixed PWM carrier is visible, with compare updates changing the duty pattern at the audio sample rate

7. Audio integration test:
   - press `SW1` or send UART `p`
   - verify the song starts
   - verify LED phrase changes still align with phrase boundaries
   - verify UART status still prints state/note/phrase

8. Pause/resume/stop test:
   - pause during `NOTE_ON`
   - verify Timer0A/DMA stop and PB6 is muted
   - resume and verify the current note restarts cleanly
   - stop/restart and verify counters do not wedge
   - expected result: `freq=0` while paused/stopped, then nonzero again after resume/play

9. Stress test:
   - let the song loop or restart repeatedly for at least one minute
   - press `SW1` during playback
   - send UART commands while audio plays
   - expected result: no hard faults, no DMA errors, no stuck note, no lost FSM control

## 15. Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Button bounce causes multiple state changes | playback feels unreliable | use fixed debounce and edge detection |
| Busy-wait delays distort note timing | poor musical accuracy | use timer-based timing, not software delay loops |
| PWM audio is too quiet or unclear | demo quality suffers | add simple filter/amplifier or switch to R-2R DAC path |
| DMA setup becomes too large late in the schedule | Milestone 5 slips | keep audio data table-driven early so backend can change later |
| UART logic duplicates button logic | harder to maintain | route both controls through common FSM events |
| DMA control table is not aligned | hard fault or no transfers | force `1024-byte` alignment and verify address in debugger |
| wrong DMA request source | no sample movement | use Timer0A as the DMA trigger and verify Timer0A events first |
| ping-pong buffer is not re-armed | audio plays briefly then stops | check primary/alternate mode and reload the completed descriptor |
| DMA ISR does too much work | missed commands or unstable audio | refill only the completed buffer and keep UART/FSM work outside ISR |
| DMA changes note timing | LEDs and music drift apart | keep note duration in SysTick/FSM; DMA only streams samples |
| pause leaves DMA running | sound continues while paused | stop Timer0A, disable the DMA channel, and mute PWM on pause/stop |

## 16. Schedule

Assuming work starts on April 13, 2026:

- April 13-17: Milestone 2 FSM design and architecture
- April 18-24: Milestone 3 audio, LED sync, and button control
- April 25-May 1: Milestone 4 UART integration and debug support
- May 2-May 5: Milestone 5 DMA integration, timing cleanup, and final verification

## 17. Recommended Design Decisions

To keep the project achievable:

- Use `PWM` for Milestone 3 unless the course explicitly requires sampled DAC audio earlier.
- Make song data table-driven from the start.
- Use a timer tick for both debounce and note duration tracking.
- Keep the FSM as the single source of truth for playback behavior.
- Treat DMA as an audio backend change, not as a rewrite of the full application.

## 18. Immediate Next Steps

1. Flash the current firmware to the TM4C board.
2. Open UART0 at `115200-8-N-1` and confirm the startup banner appears.
3. Send `d` before playback and confirm DMA counters are stable with `freq=0`.
4. Send `p` or press `SW1`; confirm music starts, phrase LEDs still change, and `d` shows increasing DMA counters.
5. Probe `PB6 / M0PWM0` if audio is unclear; verify the PWM carrier exists before debugging the speaker/amplifier circuit.
