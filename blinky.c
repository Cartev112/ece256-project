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

#define SYSTEM_CLOCK_HZ     16000000U
#define POLL_PERIOD_MS      5U
#define DEBOUNCE_SAMPLES    3U

#define LED_RED_PIN         0x02U
#define LED_BLUE_PIN        0x04U
#define LED_GREEN_PIN       0x08U
#define RGB_LED_PINS        (LED_RED_PIN | LED_BLUE_PIN | LED_GREEN_PIN)
#define SW1_PIN             0x10U

typedef enum
{
    STATE_IDLE = 0,
    STATE_PLAYING,
    STATE_PAUSED
} app_state_t;

typedef enum
{
    EVT_NONE = 0,
    EVT_SW1_PRESS,
    EVT_SONG_DONE
} app_event_t;

typedef struct
{
    app_state_t current_state;
    app_event_t event;
    app_state_t next_state;
} fsm_transition_t;

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
     */
    HWREG(SYSCTL_RCGCGPIO) |= SYSCTL_RCGCGPIO_R5;
    while((HWREG(SYSCTL_PRGPIO) & SYSCTL_PRGPIO_R5) == 0U)
    {
    }

    HWREG(GPIO_PORTF_BASE + GPIO_O_AFSEL) &= ~(RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + GPIO_O_AMSEL) &= ~(RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + GPIO_O_PCTL) &= ~0x000FFFF0U;
    HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) =
        (HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) & ~SW1_PIN) | RGB_LED_PINS;
    HWREG(GPIO_PORTF_BASE + GPIO_O_PUR) |= SW1_PIN;
    HWREG(GPIO_PORTF_BASE + GPIO_O_DEN) |= (RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (RGB_LED_PINS << 2))) = 0U;
}

static void
SetLedOutput(uint8_t ui8Pins)
{
    HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (RGB_LED_PINS << 2))) = ui8Pins;
}

static void
ApplyStateOutputs(app_context_t *psContext)
{
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

    if(eEvent == EVT_NONE)
    {
        return;
    }

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
    static bool bLastSamplePressed = false;
    static bool bStablePressed = false;
    static uint8_t ui8StableSamples = 0U;

    bool bSamplePressed;

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

    sAppContext.state = STATE_IDLE;
    sAppContext.song_index = 0U;
    sAppContext.phrase_index = 0U;
    ApplyStateOutputs(&sAppContext);

    while(1)
    {
        HandleEvent(&sAppContext, PollSwitchEvent());
        WaitForNextPoll();
    }
}
