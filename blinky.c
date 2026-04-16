//*****************************************************************************
//
// blinky.c - Milestone 2 finite state machine scaffold for the TM4C music
// player project.
//
// This version intentionally models more than "idle / playing / paused".
// The active playing region is expanded into phrase setup, note setup, note
// output, and note gap states so the FSM looks like the system we will build
// for the music player rather than a generic three-state controller.
//
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_gpio.h"
#include "inc/hw_memmap.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"

/*
 * The TM4C LaunchPad comes out of reset using the internal 16 MHz system
 * clock. Milestone 2 keeps that default to stay focused on the FSM. Later
 * audio milestones should replace this with explicit clock/timer setup.
 */
#define SYSTEM_CLOCK_HZ             16000000U

/*
 * SW1 is polled every 5 ms. The simulated playback events below are also
 * expressed in poll ticks so this demo can run without interrupts or timers.
 */
#define POLL_PERIOD_MS              5U
#define DEBOUNCE_SAMPLES            3U

/*
 * Demo timing constants. They are not real song timing yet; they just let the
 * board visibly walk through the expanded FSM before audio is implemented.
 */
#define LOAD_PHRASE_TICKS           40U     // 200 ms
#define LOAD_NOTE_TICKS             20U     // 100 ms
#define NOTE_ON_TICKS               80U     // 400 ms
#define NOTE_GAP_TICKS              30U     // 150 ms

/*
 * Simulated song structure for Milestone 2. This approximates four phrases
 * with four notes each. Milestone 3 will replace these constants with a real
 * note table for Twinkle Twinkle Little Star.
 */
#define DEMO_NOTES_PER_PHRASE       4U
#define DEMO_PHRASE_COUNT           4U
#define DEMO_TOTAL_NOTES            (DEMO_NOTES_PER_PHRASE * DEMO_PHRASE_COUNT)

/*
 * GPIO Port F bit assignments on the EK-TM4C123GXL LaunchPad:
 *   PF1 = red LED
 *   PF2 = blue LED
 *   PF3 = green LED
 *   PF4 = SW1 pushbutton, active low
 */
#define LED_RED_PIN                 0x02U
#define LED_BLUE_PIN                0x04U
#define LED_GREEN_PIN               0x08U
#define LED_YELLOW_PINS             (LED_RED_PIN | LED_GREEN_PIN)
#define LED_CYAN_PINS               (LED_BLUE_PIN | LED_GREEN_PIN)
#define LED_MAGENTA_PINS            (LED_RED_PIN | LED_BLUE_PIN)
#define LED_WHITE_PINS              (LED_RED_PIN | LED_BLUE_PIN | LED_GREEN_PIN)
#define RGB_LED_PINS                LED_WHITE_PINS
#define SW1_PIN                     0x10U

/*
 * Expanded Milestone 2 state list.
 *
 * STATE_IDLE:
 *   Waiting for the user to start the show. Playback indices are reset.
 *
 * STATE_LOAD_PHRASE:
 *   Phrase-level setup. This is where phrase LED patterns will be selected.
 *
 * STATE_LOAD_NOTE:
 *   Note-level setup. This is where frequency, duration, and rest/note status
 *   will be loaded from the song table.
 *
 * STATE_NOTE_ON:
 *   Active note output. Milestone 3 will enable PWM or DAC output here.
 *
 * STATE_NOTE_GAP:
 *   Short silence between notes. This gives the FSM a distinct place to
 *   decide whether the next step is another note, a new phrase, or completion.
 *
 * STATE_PAUSED:
 *   Playback is suspended. The code saves the state and tick count that were
 *   active before pausing so resume returns to the correct sub-state.
 *
 * STATE_SONG_COMPLETE:
 *   The simulated song has finished. A new SW1 press restarts from the top.
 */
typedef enum
{
    STATE_IDLE = 0,
    STATE_LOAD_PHRASE,
    STATE_LOAD_NOTE,
    STATE_NOTE_ON,
    STATE_NOTE_GAP,
    STATE_PAUSED,
    STATE_SONG_COMPLETE
} app_state_t;

/*
 * FSM input events.
 *
 * Hardware and timing code translate raw conditions into these abstract
 * events. The transition logic does not directly know about GPIO registers or
 * software counters.
 */
typedef enum
{
    EVT_NONE = 0,
    EVT_SW1_PRESS,
    EVT_PHRASE_READY,
    EVT_NOTE_READY,
    EVT_NOTE_DONE,
    EVT_GAP_DONE,
    EVT_PHRASE_DONE,
    EVT_SONG_DONE
} app_event_t;

/*
 * One row in the static transition table.
 */
typedef struct
{
    app_state_t current_state;
    app_event_t event;
    app_state_t next_state;
} fsm_transition_t;

/*
 * Current application state.
 *
 * song_index:
 *   The simulated note number. It will become the index into the real song
 *   table in Milestone 3.
 *
 * phrase_index:
 *   The simulated phrase number. It will drive phrase LED colors later.
 *
 * state_ticks:
 *   Number of poll intervals spent in the current state.
 *
 * resume_state / resume_ticks:
 *   Saved when pausing so resume returns to the correct active sub-state.
 */
typedef struct
{
    app_state_t state;
    app_state_t resume_state;
    uint16_t song_index;
    uint8_t phrase_index;
    uint16_t state_ticks;
    uint16_t resume_ticks;
} app_context_t;

/*
 * Milestone 2 transition table.
 *
 * Pause and resume are handled as a small special case in HandleEvent() because
 * PAUSED must return to whichever active state was interrupted.
 */
static const fsm_transition_t g_fsm_transitions[] =
{
    { STATE_IDLE,          EVT_SW1_PRESS,    STATE_LOAD_PHRASE  },
    { STATE_LOAD_PHRASE,   EVT_PHRASE_READY, STATE_LOAD_NOTE    },
    { STATE_LOAD_NOTE,     EVT_NOTE_READY,   STATE_NOTE_ON      },
    { STATE_NOTE_ON,       EVT_NOTE_DONE,    STATE_NOTE_GAP     },
    { STATE_NOTE_GAP,      EVT_GAP_DONE,     STATE_LOAD_NOTE    },
    { STATE_NOTE_GAP,      EVT_PHRASE_DONE,  STATE_LOAD_PHRASE  },
    { STATE_NOTE_GAP,      EVT_SONG_DONE,    STATE_SONG_COMPLETE },
    { STATE_SONG_COMPLETE, EVT_SW1_PRESS,    STATE_LOAD_PHRASE  }
};

static void
BoardInit(void)
{
    /*
     * Enable Port F and wait until the peripheral is ready before touching
     * any Port F registers.
     */
    HWREG(SYSCTL_RCGCGPIO) |= SYSCTL_RCGCGPIO_R5;
    while((HWREG(SYSCTL_PRGPIO) & SYSCTL_PRGPIO_R5) == 0U)
    {
    }

    /*
     * Use PF1/PF2/PF3/PF4 as plain digital GPIO. Clearing AFSEL, AMSEL, and
     * PCTL prevents alternate peripheral functions from taking over the pins.
     */
    HWREG(GPIO_PORTF_BASE + GPIO_O_AFSEL) &= ~(RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + GPIO_O_AMSEL) &= ~(RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + GPIO_O_PCTL) &= ~0x000FFFF0U;

    /*
     * The RGB LED pins are outputs; SW1 is an input with an internal pull-up
     * because the switch is active low.
     */
    HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) =
        (HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) & ~SW1_PIN) | RGB_LED_PINS;
    HWREG(GPIO_PORTF_BASE + GPIO_O_PUR) |= SW1_PIN;
    HWREG(GPIO_PORTF_BASE + GPIO_O_DEN) |= (RGB_LED_PINS | SW1_PIN);

    /*
     * Start with all LED channels off. The initial IDLE entry will turn blue
     * on immediately after the application context is initialized.
     */
    HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (RGB_LED_PINS << 2))) = 0U;
}

static void
SetLedOutput(uint8_t ui8Pins)
{
    /*
     * TM4C GPIO supports masked data aliases. This write affects only the
     * PF1/PF2/PF3 LED bits and leaves the switch input untouched.
     */
    HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (RGB_LED_PINS << 2))) = ui8Pins;
}

static bool
IsActivePlaybackState(app_state_t eState)
{
    /*
     * These states are considered the expanded "PLAYING" region. A press of
     * SW1 from any of them pauses playback.
     */
    return((eState == STATE_LOAD_PHRASE) ||
           (eState == STATE_LOAD_NOTE) ||
           (eState == STATE_NOTE_ON) ||
           (eState == STATE_NOTE_GAP));
}

static void
ApplyStateOutputs(const app_context_t *psContext)
{
    /*
     * State-to-LED mapping for board testing:
     *   IDLE          = blue
     *   LOAD_PHRASE   = yellow
     *   LOAD_NOTE     = cyan
     *   NOTE_ON       = green
     *   NOTE_GAP      = white
     *   PAUSED        = red
     *   SONG_COMPLETE = magenta
     */
    switch(psContext->state)
    {
        case STATE_IDLE:
            SetLedOutput(LED_BLUE_PIN);
            break;

        case STATE_LOAD_PHRASE:
            SetLedOutput(LED_YELLOW_PINS);
            break;

        case STATE_LOAD_NOTE:
            SetLedOutput(LED_CYAN_PINS);
            break;

        case STATE_NOTE_ON:
            SetLedOutput(LED_GREEN_PIN);
            break;

        case STATE_NOTE_GAP:
            SetLedOutput(LED_WHITE_PINS);
            break;

        case STATE_PAUSED:
            SetLedOutput(LED_RED_PIN);
            break;

        case STATE_SONG_COMPLETE:
            SetLedOutput(LED_MAGENTA_PINS);
            break;

        default:
            SetLedOutput(0U);
            break;
    }
}

static void
EnterState(app_context_t *psContext, app_state_t eNextState)
{
    /*
     * Normal state entry resets the per-state timer. PAUSED resume is handled
     * separately because it must restore the interrupted timer value.
     */
    psContext->state = eNextState;
    psContext->state_ticks = 0U;
    ApplyStateOutputs(psContext);
}

static void
ResetPlaybackPosition(app_context_t *psContext)
{
    psContext->song_index = 0U;
    psContext->phrase_index = 0U;
    psContext->resume_state = STATE_IDLE;
    psContext->resume_ticks = 0U;
}

static void
ApplyTransitionAction(app_context_t *psContext, app_event_t eEvent)
{
    /*
     * These actions update the simulated playback bookkeeping. They are kept
     * separate from the transition table so the table stays readable.
     */
    switch(eEvent)
    {
        case EVT_SW1_PRESS:
            if((psContext->state == STATE_IDLE) ||
               (psContext->state == STATE_SONG_COMPLETE))
            {
                ResetPlaybackPosition(psContext);
            }
            break;

        case EVT_GAP_DONE:
            psContext->song_index++;
            break;

        case EVT_PHRASE_DONE:
            psContext->song_index++;
            psContext->phrase_index++;
            break;

        case EVT_SONG_DONE:
            psContext->song_index = DEMO_TOTAL_NOTES;
            break;

        default:
            break;
    }
}

static void
HandleEvent(app_context_t *psContext, app_event_t eEvent)
{
    uint32_t ui32Index;

    if(eEvent == EVT_NONE)
    {
        return;
    }

    /*
     * SW1 is a control event that can interrupt any active playback sub-state.
     * The FSM saves both the current state and the elapsed state time before
     * entering PAUSED.
     */
    if((eEvent == EVT_SW1_PRESS) && IsActivePlaybackState(psContext->state))
    {
        psContext->resume_state = psContext->state;
        psContext->resume_ticks = psContext->state_ticks;
        EnterState(psContext, STATE_PAUSED);
        return;
    }

    /*
     * Pressing SW1 while paused resumes exactly where the FSM was interrupted.
     */
    if((eEvent == EVT_SW1_PRESS) && (psContext->state == STATE_PAUSED))
    {
        psContext->state = psContext->resume_state;
        psContext->state_ticks = psContext->resume_ticks;
        ApplyStateOutputs(psContext);
        return;
    }

    /*
     * Standard table-driven transition handling for all non-pause cases.
     */
    for(ui32Index = 0U;
        ui32Index < (sizeof(g_fsm_transitions) / sizeof(g_fsm_transitions[0]));
        ui32Index++)
    {
        if((g_fsm_transitions[ui32Index].current_state == psContext->state) &&
           (g_fsm_transitions[ui32Index].event == eEvent))
        {
            ApplyTransitionAction(psContext, eEvent);
            EnterState(psContext, g_fsm_transitions[ui32Index].next_state);
            return;
        }
    }
}

static app_event_t
GenerateTimedEvent(app_context_t *psContext)
{
    uint16_t ui16NextNote;

    /*
     * The timer model is deliberately simple for Milestone 2. It creates
     * artificial events after a state has been active for a fixed number of
     * polling ticks. Real note durations will replace this in Milestone 3.
     */
    if(!IsActivePlaybackState(psContext->state))
    {
        return EVT_NONE;
    }

    psContext->state_ticks++;

    switch(psContext->state)
    {
        case STATE_LOAD_PHRASE:
            if(psContext->state_ticks >= LOAD_PHRASE_TICKS)
            {
                return EVT_PHRASE_READY;
            }
            break;

        case STATE_LOAD_NOTE:
            if(psContext->state_ticks >= LOAD_NOTE_TICKS)
            {
                return EVT_NOTE_READY;
            }
            break;

        case STATE_NOTE_ON:
            if(psContext->state_ticks >= NOTE_ON_TICKS)
            {
                return EVT_NOTE_DONE;
            }
            break;

        case STATE_NOTE_GAP:
            if(psContext->state_ticks >= NOTE_GAP_TICKS)
            {
                ui16NextNote = psContext->song_index + 1U;

                if(ui16NextNote >= DEMO_TOTAL_NOTES)
                {
                    return EVT_SONG_DONE;
                }

                if((ui16NextNote % DEMO_NOTES_PER_PHRASE) == 0U)
                {
                    return EVT_PHRASE_DONE;
                }

                return EVT_GAP_DONE;
            }
            break;

        default:
            break;
    }

    return EVT_NONE;
}

static app_event_t
PollSwitchEvent(void)
{
    static bool bLastSamplePressed = false;
    static bool bStablePressed = false;
    static uint8_t ui8StableSamples = 0U;

    bool bSamplePressed;

    /*
     * SW1 is active low, so a zero on PF4 means "pressed."
     */
    bSamplePressed =
        ((HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (SW1_PIN << 2))) & SW1_PIN) ==
         0U);

    if(bSamplePressed == bLastSamplePressed)
    {
        if(ui8StableSamples < DEBOUNCE_SAMPLES)
        {
            ui8StableSamples++;
        }
    }
    else
    {
        bLastSamplePressed = bSamplePressed;
        ui8StableSamples = 1U;
    }

    /*
     * Only the released-to-pressed edge creates an FSM event. The release edge
     * updates debounce state but does not change playback state.
     */
    if((ui8StableSamples >= DEBOUNCE_SAMPLES) &&
       (bSamplePressed != bStablePressed))
    {
        bStablePressed = bSamplePressed;

        if(bStablePressed)
        {
            return EVT_SW1_PRESS;
        }
    }

    return EVT_NONE;
}

static void
WaitForNextPoll(void)
{
    volatile uint32_t ui32DelayCount;

    /*
     * Temporary software delay for Milestone 2. Hardware timers should replace
     * this before note playback is implemented.
     */
    ui32DelayCount = SYSTEM_CLOCK_HZ / (3U * (1000U / POLL_PERIOD_MS));
    while(ui32DelayCount > 0U)
    {
        ui32DelayCount--;
    }
}

int
main(void)
{
    app_context_t sAppContext;

    BoardInit();

    /*
     * Explicit initial state required by the FSM design.
     */
    sAppContext.state = STATE_IDLE;
    sAppContext.resume_state = STATE_IDLE;
    sAppContext.song_index = 0U;
    sAppContext.phrase_index = 0U;
    sAppContext.state_ticks = 0U;
    sAppContext.resume_ticks = 0U;
    ApplyStateOutputs(&sAppContext);

    while(1)
    {
        /*
         * Prioritize user input so a button press can pause before an
         * automatically generated timing event advances the FSM.
         */
        HandleEvent(&sAppContext, PollSwitchEvent());
        HandleEvent(&sAppContext, GenerateTimedEvent(&sAppContext));
        WaitForNextPoll();
    }
}
