//*****************************************************************************
//
// blinky.c - FSM-driven TM4C music player scaffold.
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
#include "inc/hw_nvic.h"
#include "inc/hw_pwm.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"
#include "inc/hw_uart.h"

/*
 * The TM4C LaunchPad comes out of reset using the internal 16 MHz system
 * clock. This milestone keeps that default to avoid introducing PLL setup
 * before we need tighter timing.
 */
#define SYSTEM_CLOCK_HZ             16000000U
#define PWM_CLOCK_HZ                SYSTEM_CLOCK_HZ

/*
 * SysTick interrupts every 5 ms. Song durations are converted to these ticks.
 * The main loop only advances button debounce and playback timing after the
 * interrupt sets a tick flag.
 */
#define SYSTICK_PERIOD_MS           5U
#define DEBOUNCE_SAMPLES            3U

/*
 * Short setup states keep the expanded FSM explicit without adding an obvious
 * delay between notes. The note duration itself comes from the song table.
 */
#define LOAD_PHRASE_TICKS           1U
#define LOAD_NOTE_TICKS             1U
#define NOTE_GAP_TICKS              10U     // 50 ms

/*
 * PB6 is M0PWM0 on the TM4C123GH6PM. Connect this pin to the input of a small
 * speaker driver, powered buzzer input, or RC filter/amplifier. Do not drive a
 * bare low-impedance speaker directly from the microcontroller pin.
 *
 * PB0-PB3 are also configured as plain digital outputs so the code can drive
 * the external LED bank used by audio.c without interfering with PB6 PWM.
 */
#define PORTB_LED_PINS             0x0FU
#define AUDIO_PWM_PIN               0x40U
#define GPIO_PCTL_PB6_M0PWM0_VALUE  0x04000000U

#define NOTE_REST_HZ                0U
#define NOTE_C4_HZ                  262U
#define NOTE_D4_HZ                  294U
#define NOTE_E4_HZ                  330U
#define NOTE_F4_HZ                  349U
#define NOTE_G4_HZ                  392U
#define NOTE_A4_HZ                  440U

#define QUARTER_NOTE_MS             400U
#define HALF_NOTE_MS                800U

#define UART_BAUD_RATE              115200U
#define UART0_RXTX_PINS             0x03U
#define GPIO_PCTL_PA0_U0RX_VALUE    0x00000001U
#define GPIO_PCTL_PA1_U0TX_VALUE    0x00000010U

static volatile bool g_bSysTickElapsed = false;
static volatile uint32_t g_ui32SystemTicks = 0U;

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
 * Expanded playback state list.
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
 *   The song has finished. A new SW1 press restarts from the top.
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
    EVT_UART_PLAY,
    EVT_UART_PAUSE,
    EVT_UART_STOP,
    EVT_UART_RESTART,
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
 * One playable item in the song table.
 *
 * frequency_hz:
 *   Target PWM frequency. A value of NOTE_REST_HZ mutes the output.
 *
 * duration_ms:
 *   How long the note should sound before entering NOTE_GAP.
 *
 * phrase_id:
 *   Phrase grouping used to synchronize LED color changes with the music.
 */
typedef struct
{
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint8_t phrase_id;
} note_t;

/*
 * Current application state.
 *
 * song_index:
 *   The active note's index in the Twinkle song table.
 *
 * phrase_index:
 *   Current phrase number, used for LED color synchronization.
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
 * Twinkle Twinkle Little Star in C major.
 *
 * The phrase IDs follow the common line structure:
 *   0: C C G G A A G
 *   1: F F E E D D C
 *   2: G G F F E E D
 *   3: G G F F E E D
 *   4: C C G G A A G
 *   5: F F E E D D C
 */
static const note_t g_twinkle_song[] =
{
    { NOTE_C4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_C4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_A4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_A4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_G4_HZ, HALF_NOTE_MS,    0U },

    { NOTE_F4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_D4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_D4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_C4_HZ, HALF_NOTE_MS,    1U },

    { NOTE_G4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_D4_HZ, HALF_NOTE_MS,    2U },

    { NOTE_G4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_D4_HZ, HALF_NOTE_MS,    3U },

    { NOTE_C4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_C4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_A4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_A4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_G4_HZ, HALF_NOTE_MS,    4U },

    { NOTE_F4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_D4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_D4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_C4_HZ, HALF_NOTE_MS,    5U }
};

#define TWINKLE_NOTE_COUNT \
    ((uint16_t)(sizeof(g_twinkle_song) / sizeof(g_twinkle_song[0])))

/*
 * Playback transition table.
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

static void AudioStop(void);
static void SetPortBLedOutput(uint8_t ui8Pins);
static void UartInit(void);
static void UartWriteChar(char cChar);
static void UartWriteString(const char *pcString);
static void UartWriteUInt(uint32_t ui32Value);
static bool UartReadChar(char *pcChar);
static void UartWriteStatus(const app_context_t *psContext, const char *pcPrefix);
static void HandleUartCommand(app_context_t *psContext, char cCommand);
static void HandleEvent(app_context_t *psContext, app_event_t eEvent);

static void
BoardInit(void)
{
    /*
     * Enable Port A for UART0 pins, Port F for the onboard RGB LED and SW1,
     * and Port B plus PWM0 for PB0-PB3 LED output and PB6/M0PWM0 audio.
     */
    HWREG(SYSCTL_RCGCGPIO) |=
        (SYSCTL_RCGCGPIO_R5 | SYSCTL_RCGCGPIO_R1 | SYSCTL_RCGCGPIO_R0);
    HWREG(SYSCTL_RCGCPWM) |= SYSCTL_RCGCPWM_R0;
    while((HWREG(SYSCTL_PRGPIO) &
           (SYSCTL_PRGPIO_R5 | SYSCTL_PRGPIO_R1 | SYSCTL_PRGPIO_R0)) !=
          (SYSCTL_PRGPIO_R5 | SYSCTL_PRGPIO_R1 | SYSCTL_PRGPIO_R0))
    {
    }
    while((HWREG(SYSCTL_PRPWM) & SYSCTL_PRPWM_R0) == 0U)
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

    /*
     * PB0-PB3 are used as plain digital outputs for the external LED bank from
     * audio.c. PB6 becomes M0PWM0. The PCTL field for PB6 is bits 27:24.
     */
    HWREG(GPIO_PORTB_BASE + GPIO_O_AFSEL) &= ~PORTB_LED_PINS;
    HWREG(GPIO_PORTB_BASE + GPIO_O_AFSEL) |= AUDIO_PWM_PIN;
    HWREG(GPIO_PORTB_BASE + GPIO_O_AMSEL) &= ~(PORTB_LED_PINS | AUDIO_PWM_PIN);
    HWREG(GPIO_PORTB_BASE + GPIO_O_PCTL) =
        (HWREG(GPIO_PORTB_BASE + GPIO_O_PCTL) & ~0x0F00000FU) |
        GPIO_PCTL_PB6_M0PWM0_VALUE;
    HWREG(GPIO_PORTB_BASE + GPIO_O_DIR) |= PORTB_LED_PINS;
    HWREG(GPIO_PORTB_BASE + GPIO_O_DEN) |= (PORTB_LED_PINS | AUDIO_PWM_PIN);
    SetPortBLedOutput(0U);

    /*
     * PWM generator 0 controls M0PWM0. The generator action drives the output
     * high at LOAD and low at comparator A, producing a 50% duty square wave
     * after AudioStart() loads a frequency.
     */
    HWREG(PWM0_BASE + PWM_O_0_CTL) = 0U;
    HWREG(PWM0_BASE + PWM_O_0_GENA) =
        PWM_0_GENA_ACTLOAD_ONE | PWM_0_GENA_ACTCMPAD_ZERO;
    HWREG(PWM0_BASE + PWM_O_ENABLE) &= ~PWM_ENABLE_PWM0EN;
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

static void
SetPortBLedOutput(uint8_t ui8Pins)
{
    /*
     * Mirror status onto the external Port B LED bank used in audio.c. PB0-PB3
     * are normal GPIO outputs, so the low nibble can be written directly using
     * a masked data alias without affecting PB6 PWM.
     */
    HWREG(GPIO_PORTB_BASE + (GPIO_O_DATA + (PORTB_LED_PINS << 2))) =
        (ui8Pins & PORTB_LED_PINS);
}

static uint16_t
MsToTicks(uint16_t ui16DurationMs)
{
    uint16_t ui16Ticks;

    ui16Ticks = (uint16_t)(ui16DurationMs / SYSTICK_PERIOD_MS);
    if(ui16Ticks == 0U)
    {
        ui16Ticks = 1U;
    }

    return ui16Ticks;
}

void
SysTick_Handler(void)
{
    /*
     * Keep the ISR short. It only records that one scheduler tick elapsed;
     * the main loop handles debounce, FSM transitions, LED updates, and audio
     * state changes outside interrupt context.
     */
    g_ui32SystemTicks++;
    g_bSysTickElapsed = true;
}

static void
SysTickInit(void)
{
    uint32_t ui32Reload;

    ui32Reload = ((SYSTEM_CLOCK_HZ / 1000U) * SYSTICK_PERIOD_MS) - 1U;

    /*
     * SysTick has a 24-bit reload register. The 5 ms reload at 16 MHz is
     * 79,999, so it is comfortably within range.
     */
    HWREG(NVIC_ST_CTRL) = 0U;
    HWREG(NVIC_ST_RELOAD) = ui32Reload & NVIC_ST_RELOAD_M;
    HWREG(NVIC_ST_CURRENT) = 0U;
    HWREG(NVIC_ST_CTRL) =
        NVIC_ST_CTRL_CLK_SRC | NVIC_ST_CTRL_INTEN | NVIC_ST_CTRL_ENABLE;
}

static void
UartInit(void)
{
    /*
     * UART0 uses PA0/PA1. With a 16 MHz system clock and 115200 baud:
     *
     * BRD = 16,000,000 / (16 * 115200) = 8.6805
     * IBRD = 8
     * FBRD = round(0.6805 * 64) = 44
     */
    HWREG(SYSCTL_RCGCUART) |= SYSCTL_RCGCUART_R0;
    while((HWREG(SYSCTL_PRUART) & SYSCTL_PRUART_R0) == 0U)
    {
    }

    HWREG(GPIO_PORTA_BASE + GPIO_O_AFSEL) |= UART0_RXTX_PINS;
    HWREG(GPIO_PORTA_BASE + GPIO_O_AMSEL) &= ~UART0_RXTX_PINS;
    HWREG(GPIO_PORTA_BASE + GPIO_O_PCTL) =
        (HWREG(GPIO_PORTA_BASE + GPIO_O_PCTL) & ~0x000000FFU) |
        GPIO_PCTL_PA0_U0RX_VALUE | GPIO_PCTL_PA1_U0TX_VALUE;
    HWREG(GPIO_PORTA_BASE + GPIO_O_DEN) |= UART0_RXTX_PINS;

    HWREG(UART0_BASE + UART_O_CTL) = 0U;
    HWREG(UART0_BASE + UART_O_IBRD) = 8U;
    HWREG(UART0_BASE + UART_O_FBRD) = 44U;
    HWREG(UART0_BASE + UART_O_LCRH) = UART_LCRH_WLEN_8 | UART_LCRH_FEN;
    HWREG(UART0_BASE + UART_O_CTL) =
        UART_CTL_UARTEN | UART_CTL_TXE | UART_CTL_RXE;
}

static void
UartWriteChar(char cChar)
{
    while((HWREG(UART0_BASE + UART_O_FR) & UART_FR_TXFF) != 0U)
    {
    }

    HWREG(UART0_BASE + UART_O_DR) = (uint32_t)cChar;
}

static void
UartWriteString(const char *pcString)
{
    while(*pcString != '\0')
    {
        if(*pcString == '\n')
        {
            UartWriteChar('\r');
        }

        UartWriteChar(*pcString);
        pcString++;
    }
}

static void
UartWriteUInt(uint32_t ui32Value)
{
    char acDigits[10];
    uint32_t ui32Index;

    if(ui32Value == 0U)
    {
        UartWriteChar('0');
        return;
    }

    ui32Index = 0U;
    while(ui32Value > 0U)
    {
        acDigits[ui32Index] = (char)('0' + (ui32Value % 10U));
        ui32Value /= 10U;
        ui32Index++;
    }

    while(ui32Index > 0U)
    {
        ui32Index--;
        UartWriteChar(acDigits[ui32Index]);
    }
}

static bool
UartReadChar(char *pcChar)
{
    if((HWREG(UART0_BASE + UART_O_FR) & UART_FR_RXFE) != 0U)
    {
        return false;
    }

    *pcChar = (char)(HWREG(UART0_BASE + UART_O_DR) & UART_DR_DATA_M);
    return true;
}

static void
UartWriteStatus(const app_context_t *psContext, const char *pcPrefix)
{
    UartWriteString(pcPrefix);
    UartWriteString(": tick=");
    UartWriteUInt(g_ui32SystemTicks);
    UartWriteString(" state=");

    switch(psContext->state)
    {
        case STATE_IDLE:
            UartWriteString("IDLE");
            break;

        case STATE_LOAD_PHRASE:
            UartWriteString("LOAD_PHRASE");
            break;

        case STATE_LOAD_NOTE:
            UartWriteString("LOAD_NOTE");
            break;

        case STATE_NOTE_ON:
            UartWriteString("NOTE_ON");
            break;

        case STATE_NOTE_GAP:
            UartWriteString("NOTE_GAP");
            break;

        case STATE_PAUSED:
            UartWriteString("PAUSED");
            break;

        case STATE_SONG_COMPLETE:
            UartWriteString("SONG_COMPLETE");
            break;

        default:
            UartWriteString("UNKNOWN");
            break;
    }

    UartWriteString(" note=");
    UartWriteUInt(psContext->song_index);
    UartWriteString(" phrase=");
    UartWriteUInt(psContext->phrase_index);
    UartWriteChar('\n');
}

static uint8_t
PhraseToLedColor(uint8_t ui8PhraseId)
{
    /*
     * There are only three physical LED channels, so the six phrases use a
     * repeatable color cycle.
     */
    switch(ui8PhraseId % 6U)
    {
        case 0U:
            return LED_RED_PIN;

        case 1U:
            return LED_GREEN_PIN;

        case 2U:
            return LED_BLUE_PIN;

        case 3U:
            return LED_YELLOW_PINS;

        case 4U:
            return LED_CYAN_PINS;

        default:
            return LED_MAGENTA_PINS;
    }
}

static uint8_t
PhraseToPortBPattern(uint8_t ui8PhraseId)
{
    /*
     * Reuse the external LED patterns that appeared in audio.c so the same
     * wiring on PB0-PB3 still gives recognizable phrase-level feedback.
     */
    switch(ui8PhraseId)
    {
        case 0U:
            return 0x05U;

        case 1U:
            return 0x0AU;

        case 2U:
            return 0x00U;

        case 3U:
            return 0x0FU;

        case 4U:
            return 0x05U;

        default:
            return 0x0AU;
    }
}

static void
HandleUartCommand(app_context_t *psContext, char cCommand)
{
    if((cCommand >= 'A') && (cCommand <= 'Z'))
    {
        cCommand = (char)(cCommand - 'A' + 'a');
    }

    switch(cCommand)
    {
        case 'p':
            HandleEvent(psContext, EVT_UART_PLAY);
            break;

        case 'a':
            HandleEvent(psContext, EVT_UART_PAUSE);
            break;

        case 's':
            HandleEvent(psContext, EVT_UART_STOP);
            break;

        case 'r':
            HandleEvent(psContext, EVT_UART_RESTART);
            break;

        case 'h':
        case '?':
            UartWriteString("Commands: p=play/resume a=pause s=stop r=restart h=?=help\n");
            UartWriteStatus(psContext, "STATUS");
            break;

        case '\r':
        case '\n':
            break;

        default:
            UartWriteString("Unknown command: ");
            UartWriteChar(cCommand);
            UartWriteChar('\n');
            break;
    }
}

static void
AudioStop(void)
{
    HWREG(PWM0_BASE + PWM_O_ENABLE) &= ~PWM_ENABLE_PWM0EN;
    HWREG(PWM0_BASE + PWM_O_0_CTL) &= ~PWM_0_CTL_ENABLE;
}

static void
AudioStart(uint16_t ui16FrequencyHz)
{
    uint32_t ui32Load;

    if(ui16FrequencyHz == NOTE_REST_HZ)
    {
        AudioStop();
        return;
    }

    ui32Load = (PWM_CLOCK_HZ / (uint32_t)ui16FrequencyHz) - 1U;

    /*
     * The melody frequencies are low enough that the load values fit in the
     * 16-bit PWM load register at the default 16 MHz clock.
     */
    HWREG(PWM0_BASE + PWM_O_0_CTL) &= ~PWM_0_CTL_ENABLE;
    HWREG(PWM0_BASE + PWM_O_0_LOAD) = ui32Load;
    HWREG(PWM0_BASE + PWM_O_0_CMPA) = ui32Load / 2U;
    HWREG(PWM0_BASE + PWM_O_0_CTL) |= PWM_0_CTL_ENABLE;
    HWREG(PWM0_BASE + PWM_O_ENABLE) |= PWM_ENABLE_PWM0EN;
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
    uint8_t ui8RgbOutput;
    uint8_t ui8PortBOutput;

    /*
     * Milestone 3 uses phrase colors during the active playback region. This
     * keeps the visible LED behavior tied to the song data instead of just the
     * FSM state name.
     */
    switch(psContext->state)
    {
        case STATE_IDLE:
            ui8RgbOutput = LED_BLUE_PIN;
            ui8PortBOutput = 0x01U;
            break;

        case STATE_LOAD_PHRASE:
        case STATE_LOAD_NOTE:
        case STATE_NOTE_ON:
        case STATE_NOTE_GAP:
            ui8RgbOutput = PhraseToLedColor(psContext->phrase_index);
            ui8PortBOutput = PhraseToPortBPattern(psContext->phrase_index);
            break;

        case STATE_PAUSED:
            ui8RgbOutput = LED_RED_PIN;
            ui8PortBOutput = 0x08U;
            break;

        case STATE_SONG_COMPLETE:
            ui8RgbOutput = LED_MAGENTA_PINS;
            ui8PortBOutput = 0x0FU;
            break;

        default:
            ui8RgbOutput = 0U;
            ui8PortBOutput = 0U;
            break;
    }

    SetLedOutput(ui8RgbOutput);
    SetPortBLedOutput(ui8PortBOutput);
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

    switch(eNextState)
    {
        case STATE_IDLE:
        case STATE_LOAD_PHRASE:
        case STATE_LOAD_NOTE:
        case STATE_NOTE_GAP:
        case STATE_PAUSED:
        case STATE_SONG_COMPLETE:
            AudioStop();
            break;

        case STATE_NOTE_ON:
            if(psContext->song_index < TWINKLE_NOTE_COUNT)
            {
                AudioStart(g_twinkle_song[psContext->song_index].frequency_hz);
            }
            else
            {
                AudioStop();
            }
            break;

        default:
            AudioStop();
            break;
    }

    ApplyStateOutputs(psContext);
    UartWriteStatus(psContext, "STATE");
}

static void
ResetPlaybackPosition(app_context_t *psContext)
{
    psContext->song_index = 0U;
    psContext->phrase_index = g_twinkle_song[0].phrase_id;
    psContext->resume_state = STATE_IDLE;
    psContext->resume_ticks = 0U;
}

static void
ApplyTransitionAction(app_context_t *psContext, app_event_t eEvent)
{
    /*
     * These actions update the playback bookkeeping. They are kept
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
            psContext->phrase_index = g_twinkle_song[psContext->song_index].phrase_id;
            break;

        case EVT_PHRASE_DONE:
            psContext->song_index++;
            psContext->phrase_index = g_twinkle_song[psContext->song_index].phrase_id;
            break;

        case EVT_SONG_DONE:
            psContext->song_index = TWINKLE_NOTE_COUNT;
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
     * UART play/resume follows the same user-visible behavior as SW1 without
     * introducing a second control path for the common transitions.
     */
    if(eEvent == EVT_UART_PLAY)
    {
        if((psContext->state == STATE_IDLE) ||
           (psContext->state == STATE_PAUSED) ||
           (psContext->state == STATE_SONG_COMPLETE))
        {
            HandleEvent(psContext, EVT_SW1_PRESS);
        }
        return;
    }

    if(eEvent == EVT_UART_PAUSE)
    {
        if(IsActivePlaybackState(psContext->state))
        {
            HandleEvent(psContext, EVT_SW1_PRESS);
        }
        return;
    }

    if(eEvent == EVT_UART_STOP)
    {
        ResetPlaybackPosition(psContext);
        EnterState(psContext, STATE_IDLE);
        return;
    }

    if(eEvent == EVT_UART_RESTART)
    {
        ResetPlaybackPosition(psContext);
        EnterState(psContext, STATE_LOAD_PHRASE);
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
        if(psContext->state == STATE_NOTE_ON)
        {
            if(psContext->song_index < TWINKLE_NOTE_COUNT)
            {
                AudioStart(g_twinkle_song[psContext->song_index].frequency_hz);
            }
        }
        ApplyStateOutputs(psContext);
        UartWriteStatus(psContext, "STATE");
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
    uint16_t ui16NoteTicks;

    /*
     * The timing source is still a polling loop, but NOTE_ON now uses the real
     * duration from the Twinkle song table.
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
            if(psContext->song_index >= TWINKLE_NOTE_COUNT)
            {
                return EVT_SONG_DONE;
            }

            ui16NoteTicks =
                MsToTicks(g_twinkle_song[psContext->song_index].duration_ms);
            if(psContext->state_ticks >= ui16NoteTicks)
            {
                return EVT_NOTE_DONE;
            }
            break;

        case STATE_NOTE_GAP:
            if(psContext->state_ticks >= NOTE_GAP_TICKS)
            {
                ui16NextNote = psContext->song_index + 1U;

                if(ui16NextNote >= TWINKLE_NOTE_COUNT)
                {
                    return EVT_SONG_DONE;
                }

                if(g_twinkle_song[ui16NextNote].phrase_id !=
                   psContext->phrase_index)
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

int
main(void)
{
    app_context_t sAppContext;
    char cRxChar;

    BoardInit();
    UartInit();
    SysTickInit();

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
    UartWriteString("UART0 ready at 115200 8-N-1\n");
    UartWriteString("Commands: p=play/resume a=pause s=stop r=restart h=?=help\n");
    UartWriteStatus(&sAppContext, "STATE");

    while(1)
    {
        while(UartReadChar(&cRxChar))
        {
            HandleUartCommand(&sAppContext, cRxChar);
        }

        /*
         * SysTick provides the time base. The main loop does the actual work
         * once per tick so the ISR stays small and predictable.
         */
        if(g_bSysTickElapsed)
        {
            g_bSysTickElapsed = false;

            /*
             * Prioritize user input so a button press can pause before an
             * automatically generated timing event advances the FSM.
             */
            HandleEvent(&sAppContext, PollSwitchEvent());
            HandleEvent(&sAppContext, GenerateTimedEvent(&sAppContext));
        }
    }
}
