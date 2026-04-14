//*****************************************************************************
//
// blinky.c - Milestone 2 finite state machine scaffold for the TM4C music
// player project.
//
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_gpio.h"
#include "inc/hw_memmap.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"

/*
 * The TM4C LaunchPad comes out of reset using the internal system clock.
 * For this milestone we intentionally keep that default clocking model and
 * avoid PLL setup so the FSM example stays focused on state behavior rather
 * than clock configuration.
 */
#define SYSTEM_CLOCK_HZ     16000000U

/*
 * The button is sampled periodically instead of using interrupts. A 5 ms
 * polling interval is slow enough to be simple and stable, but still fast
 * enough that the board feels responsive when SW1 is pressed by hand.
 */
#define POLL_PERIOD_MS      5U

/*
 * A new switch level must be observed for this many consecutive samples
 * before the software accepts it as the debounced state.
 */
#define DEBOUNCE_SAMPLES    3U

/*
 * GPIO Port F bit assignments on the EK-TM4C123GXL LaunchPad:
 *   PF1 = red LED
 *   PF2 = blue LED
 *   PF3 = green LED
 *   PF4 = SW1 pushbutton, active low
 */
#define LED_RED_PIN         0x02U
#define LED_BLUE_PIN        0x04U
#define LED_GREEN_PIN       0x08U
#define RGB_LED_PINS        (LED_RED_PIN | LED_BLUE_PIN | LED_GREEN_PIN)
#define SW1_PIN             0x10U

/*
 * These are the three required Milestone 2 playback states.
 *
 * IDLE:
 *   The system is waiting for the user to start the show.
 *
 * PLAYING:
 *   The system is conceptually "running." Milestone 2 does not yet generate
 *   audio, but this state stands in for active song playback.
 *
 * PAUSED:
 *   The system has been temporarily halted and should later resume from the
 *   current playback position.
 */
typedef enum
{
    STATE_IDLE = 0,
    STATE_PLAYING,
    STATE_PAUSED
} app_state_t;

/*
 * Events are the FSM inputs. The state machine never directly examines the
 * pushbutton hardware; hardware sampling code translates raw input into one
 * of these abstract events first.
 */
typedef enum
{
    EVT_NONE = 0,
    EVT_SW1_PRESS,
    EVT_SONG_DONE
} app_event_t;

/*
 * Each row describes one legal transition:
 *   current_state + event -> next_state
 *
 * Keeping the transitions in a table makes the state diagram explicit in
 * code and keeps the event handler simple.
 */
typedef struct
{
    app_state_t current_state;
    app_event_t event;
    app_state_t next_state;
} fsm_transition_t;

/*
 * This structure is the software model of the player's current status.
 * Milestone 2 only uses state, song_index, and phrase_index as placeholders,
 * but those fields are already laid out for Milestone 3 when real playback
 * timing and phrase-based LED changes will be added.
 */
typedef struct
{
    app_state_t state;
    uint16_t song_index;
    uint8_t phrase_index;
} app_context_t;

/*
 * Milestone 2 FSM definition
 *
 * States:
 *   IDLE, PLAYING, PAUSED
 *
 * Inputs:
 *   EVT_SW1_PRESS, EVT_SONG_DONE
 *
 * Initial state:
 *   IDLE
 *
 * Outputs:
 *   IDLE    -> blue LED, song position reset
 *   PLAYING -> green LED
 *   PAUSED  -> red LED
 */
static const fsm_transition_t g_fsm_transitions[] =
{
    { STATE_IDLE,    EVT_SW1_PRESS, STATE_PLAYING },
    { STATE_PLAYING, EVT_SW1_PRESS, STATE_PAUSED  },
    { STATE_PAUSED,  EVT_SW1_PRESS, STATE_PLAYING },
    { STATE_PLAYING, EVT_SONG_DONE, STATE_IDLE    }
};

static void
BoardInit(void)
{
    /*
     * Milestone 2 keeps the reset-default 16 MHz system clock and only
     * configures the GPIO needed by the FSM demo.
     *
     * We enable Port F because it contains both the onboard RGB LED and SW1.
     * The ready check ensures the peripheral clock is active before any
     * registers on that port are touched.
     */
    HWREG(SYSCTL_RCGCGPIO) |= SYSCTL_RCGCGPIO_R5;
    while((HWREG(SYSCTL_PRGPIO) & SYSCTL_PRGPIO_R5) == 0U)
    {
    }

    /*
     * Disable alternate and analog functions on the LED and switch pins so
     * they behave as straightforward digital GPIO.
     */
    HWREG(GPIO_PORTF_BASE + GPIO_O_AFSEL) &= ~(RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + GPIO_O_AMSEL) &= ~(RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + GPIO_O_PCTL) &= ~0x000FFFF0U;

    /*
     * LEDs are outputs; SW1 remains an input.
     */
    HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) =
        (HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) & ~SW1_PIN) | RGB_LED_PINS;

    /*
     * SW1 is wired active low, so the internal pull-up keeps the pin at logic
     * high when the switch is not pressed.
     */
    HWREG(GPIO_PORTF_BASE + GPIO_O_PUR) |= SW1_PIN;

    /*
     * Digital-enable the LED and switch pins, then start with all LEDs off.
     */
    HWREG(GPIO_PORTF_BASE + GPIO_O_DEN) |= (RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (RGB_LED_PINS << 2))) = 0U;
}

static void
SetLedOutput(uint8_t ui8Pins)
{
    /*
     * TM4C GPIO uses an address-masked data access scheme. Shifting the pin
     * mask by two selects a masked data alias so that only the RGB bits are
     * written here.
     */
    HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (RGB_LED_PINS << 2))) = ui8Pins;
}

static void
ApplyStateOutputs(app_context_t *psContext)
{
    /*
     * This function owns the outputs associated with each state.
     * For Milestone 2 the visible output is just LED color, but the reset of
     * song/phrase indices in IDLE already mirrors the intended playback model.
     */
    switch(psContext->state)
    {
        case STATE_IDLE:
            psContext->song_index = 0U;
            psContext->phrase_index = 0U;
            SetLedOutput(LED_BLUE_PIN);
            break;

        case STATE_PLAYING:
            SetLedOutput(LED_GREEN_PIN);
            break;

        case STATE_PAUSED:
            SetLedOutput(LED_RED_PIN);
            break;

        default:
            SetLedOutput(0U);
            break;
    }
}

static void
HandleEvent(app_context_t *psContext, app_event_t eEvent)
{
    uint32_t ui32Index;

    /*
     * No event means there is nothing to do this loop iteration.
     */
    if(eEvent == EVT_NONE)
    {
        return;
    }

    /*
     * Search the transition table for a row that matches the current state
     * and incoming event. When a legal transition is found, move to the next
     * state and immediately apply that state's outputs.
     *
     * If no row matches, the event is ignored. That is acceptable here and is
     * often desirable in FSMs because not every event is meaningful in every
     * state.
     */
    for(ui32Index = 0U;
        ui32Index < (sizeof(g_fsm_transitions) / sizeof(g_fsm_transitions[0]));
        ui32Index++)
    {
        if((g_fsm_transitions[ui32Index].current_state == psContext->state) &&
           (g_fsm_transitions[ui32Index].event == eEvent))
        {
            psContext->state = g_fsm_transitions[ui32Index].next_state;
            ApplyStateOutputs(psContext);
            return;
        }
    }
}

static app_event_t
PollSwitchEvent(void)
{
    /*
     * These static variables preserve debounce history across calls.
     *
     * bLastSamplePressed:
     *   Most recent raw sample level from SW1.
     *
     * bStablePressed:
     *   Last accepted debounced switch level.
     *
     * ui8StableSamples:
     *   Number of consecutive polls that matched bLastSamplePressed.
     */
    static bool bLastSamplePressed = false;
    static bool bStablePressed = false;
    static uint8_t ui8StableSamples = 0U;

    bool bSamplePressed;

    /*
     * SW1 is active low. A raw read of zero on PF4 therefore means
     * "the button is currently pressed."
     */
    bSamplePressed =
        ((HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (SW1_PIN << 2))) & SW1_PIN) ==
         0U);

    /*
     * Standard debounce strategy:
     *   - if the new sample matches the previous raw sample, increment the
     *     stability counter
     *   - if the sample changes, restart the counter
     */
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
     * Once the raw level has remained stable long enough, compare it to the
     * last debounced level. A change from released to pressed generates the
     * single button event consumed by the FSM.
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
     * This is a simple busy-wait used only for the milestone demo. The magic
     * factor of 3 comes from the approximate cycle cost of a decrement-and-test
     * software delay loop on this class of microcontroller.
     *
     * Later milestones should replace this with SysTick or a hardware timer so
     * note timing, pause behavior, and UART integration are all time-based
     * rather than loop-speed based.
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

    /*
     * Bring the board GPIO into a known state before the state machine starts.
     */
    BoardInit();

    /*
     * The specification says the initial state is IDLE. ApplyStateOutputs()
     * makes that visible on the board immediately by setting the LED to blue
     * and resetting playback bookkeeping fields.
     */
    sAppContext.state = STATE_IDLE;
    sAppContext.song_index = 0U;
    sAppContext.phrase_index = 0U;
    ApplyStateOutputs(&sAppContext);

    /*
     * Main control loop:
     *   1. sample SW1 and convert raw hardware input to an abstract event
     *   2. feed that event into the FSM
     *   3. wait until the next poll interval
     *
     * Milestone 3 will extend this loop with note timing, audio output, and
     * phrase-based LED synchronization.
     */
    while(1)
    {
        HandleEvent(&sAppContext, PollSwitchEvent());
        WaitForNextPoll();
    }
}
