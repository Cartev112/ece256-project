#include <stdint.h>
#include "app_types.h"
#include "board_pins.h"
#include "led.h"
#include "inc/hw_gpio.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"

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
     * Mirror status onto the external Port B LED bank. PB0-PB3 are normal GPIO
     * outputs, so this masked write does not affect PB6 PWM.
     */
    HWREG(GPIO_PORTB_BASE + (GPIO_O_DATA + (PORTB_LED_PINS << 2))) =
        (ui8Pins & PORTB_LED_PINS);
}

static uint8_t
PhraseToLedColor(uint8_t ui8PhraseId)
{
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
        case 5U:
            return 0x0AU;
        case 6U:
            return 0x05U;
        case 7U:
            return 0x0AU;
        case 8U:
            return 0x05U;
        case 9U:
            return 0x0AU;
        case 10U:
            return 0x00U;
        case 11U:
            return 0x0FU;
        default:
            return 0x0AU;
    }
}

void
LedApplyStateOutputs(const app_context_t *psContext)
{
    uint8_t ui8RgbOutput;
    uint8_t ui8PortBOutput;

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
