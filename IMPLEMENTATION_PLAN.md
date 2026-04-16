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

- `main.c` or `blinky.c`
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
- `audio.[ch]`
  - PWM or DAC setup
  - note-to-frequency handling
  - sample/tone generation
- `led.[ch]`
  - RGB initialization
  - phrase-based color updates
- `button.[ch]`
  - `SW1` debounce
  - edge detection
- `uart.[ch]` for Milestone 4
  - UART0 init
  - transmit/receive helpers
- `dma.[ch]` for Milestone 5
  - `uDMA` channel config
  - ping-pong or circular buffer support

For the current small repo, it is acceptable to reach Milestone 3 in one source file first, then split into modules once the behavior is stable.

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

#### PWM plan

- Configure a PWM-capable pin.
- For each note:
  - compute load value from target note frequency
  - set a 50% duty cycle
  - enable PWM while note is active
- For rests:
  - disable PWM output or set duty cycle to zero
- Use a periodic timer or SysTick to measure note duration in milliseconds

This will not sound like a rich instrument, but it is enough for Milestone 3.

### Milestone 5 Audio Path With DMA

If DMA is required as a true integration milestone, move from note-level square-wave updates to sample-buffer streaming:

- Precompute a short waveform table or fill buffers on the fly.
- Use a timer-triggered peripheral request.
- Feed PWM compare updates or GPIO DAC samples through `uDMA`.
- Use half-buffer/full-buffer interrupts to refill the next audio block.

That gives a clearer reason for DMA than simply changing PWM frequency once per note.

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

- Poll every `5 ms` using `SysTick` or a periodic timer.
- Require `3` consecutive stable samples before accepting a state change.
- Generate one event on the `released -> pressed` transition.

This is simple, deterministic, and enough for a single-button interface.

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
- Interrupt-driven RX is preferred but polling is acceptable at first

### Integration Rule

UART must inject the same FSM events used by the button path. Do not create separate control logic for UART and `SW1`.

## 11. DMA Integration Plan

### Objectives for Milestone 5

- Demonstrate that audio streaming uses `uDMA`.
- Keep the main loop free for FSM management, button handling, LED updates, and UART.

### Proposed DMA Design

- Allocate two audio buffers, for example `buffer_a` and `buffer_b`
- Configure `uDMA` in ping-pong mode if supported by the chosen peripheral path
- Timer defines audio sample rate
- DMA moves sample values to:
  - PWM compare register, or
  - GPIO data register for an `R-2R DAC`
- Half/full transfer completion triggers buffer refill

### Why This Matters

DMA should handle high-rate repetitive sample movement. The CPU should only:

- track song progress
- select the next note or waveform segment
- update phrase LEDs
- process user input and UART

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
11. Refactor audio into buffered sample output.
12. Add `uDMA` setup and ping-pong buffer handling.
13. Re-test timing, control response, and code warnings after each milestone.

## 14. Test Plan

### Functional Tests

- On reset, system enters `IDLE`.
- First `SW1` press starts playback from the first note.
- Second `SW1` press pauses playback without losing position.
- Third `SW1` press resumes from the paused note.
- LED color changes only at phrase boundaries.
- Song completes and enters the done/idle end state correctly.

### Timing Tests

- Verify note lengths are consistent by ear and with scope/logic analyzer if available.
- Verify phrase LED changes line up with intended musical sections.
- Verify debounce prevents double-triggering from one press.

### Integration Tests

- UART commands produce the same behavior as button events.
- DMA audio still honors pause/resume and song restart.
- Build completes with no warnings in Debug configuration.

## 15. Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Button bounce causes multiple state changes | playback feels unreliable | use fixed debounce and edge detection |
| Busy-wait delays distort note timing | poor musical accuracy | use timer-based timing, not software delay loops |
| PWM audio is too quiet or unclear | demo quality suffers | add simple filter/amplifier or switch to R-2R DAC path |
| DMA setup becomes too large late in the schedule | Milestone 5 slips | keep audio data table-driven early so backend can change later |
| UART logic duplicates button logic | harder to maintain | route both controls through common FSM events |

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

1. Confirm the exact audio hardware path for Milestone 3: `PWM` or `R-2R DAC`.
2. Enter the `Twinkle Twinkle` note table and phrase boundaries.
3. Implement `IDLE`, `PLAYING`, `PAUSED`, and `SONG_DONE`.
4. Replace delay-loop timing with timer-based timing.
5. Demo button-controlled playback before adding UART and DMA.
