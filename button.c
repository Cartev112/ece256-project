#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "app_types.h"
#include "board_pins.h"
#include "button.h"
#include "inc/hw_gpio.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_nvic.h"
#include "inc/hw_types.h"

#define GPIOF_NVIC_ENABLE_BIT       (1U << (INT_GPIOF - 16U))

static volatile bool g_bSw1PressCandidate = false;

static bool
ReadSw1Pressed(void)
{
    /*
     * SW1 is active low, so a zero on PF4 means "pressed."
     */
    return((HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (SW1_PIN << 2))) &
            SW1_PIN) == 0U);
}

static void
EnableSw1Interrupt(void)
{
    HWREG(GPIO_PORTF_BASE + GPIO_O_ICR) = SW1_PIN;
    HWREG(GPIO_PORTF_BASE + GPIO_O_IM) |= SW1_PIN;
}

void
ButtonInit(void)
{
    /*
     * PF4 is already configured as a pulled-up GPIO input by BoardInit().
     * Configure its interrupt as edge-sensitive, single-edge, falling-edge.
     * Because SW1 is active low, the falling edge is the press edge.
     */
    HWREG(GPIO_PORTF_BASE + GPIO_O_IM) &= ~SW1_PIN;
    HWREG(GPIO_PORTF_BASE + GPIO_O_IS) &= ~SW1_PIN;
    HWREG(GPIO_PORTF_BASE + GPIO_O_IBE) &= ~SW1_PIN;
    HWREG(GPIO_PORTF_BASE + GPIO_O_IEV) &= ~SW1_PIN;
    EnableSw1Interrupt();

    /*
     * GPIO Port F is interrupt 30 in the NVIC external interrupt numbering.
     */
    HWREG(NVIC_EN0) |= GPIOF_NVIC_ENABLE_BIT;
}

void
GPIOF_Handler(void)
{
    if((HWREG(GPIO_PORTF_BASE + GPIO_O_MIS) & SW1_PIN) != 0U)
    {
        /*
         * The ISR only latches that a raw press edge occurred. The interrupt is
         * masked until the SysTick-driven debounce code confirms the press and
         * later observes a stable release.
         */
        HWREG(GPIO_PORTF_BASE + GPIO_O_IM) &= ~SW1_PIN;
        HWREG(GPIO_PORTF_BASE + GPIO_O_ICR) = SW1_PIN;
        g_bSw1PressCandidate = true;
    }
}

app_event_t
ButtonProcessEvent(void)
{
    static bool bDebounceActive = false;
    static bool bPressReported = false;
    static bool bLastSamplePressed = false;
    static uint8_t ui8StableSamples = 0U;

    bool bSamplePressed;

    if(g_bSw1PressCandidate)
    {
        g_bSw1PressCandidate = false;
        bDebounceActive = true;
        bPressReported = false;
        bLastSamplePressed = ReadSw1Pressed();
        ui8StableSamples = 1U;
    }

    if(!bDebounceActive)
    {
        return EVT_NONE;
    }

    bSamplePressed = ReadSw1Pressed();

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

    if(ui8StableSamples < DEBOUNCE_SAMPLES)
    {
        return EVT_NONE;
    }

    if(!bPressReported)
    {
        if(bSamplePressed)
        {
            bPressReported = true;
            return EVT_SW1_PRESS;
        }

        /*
         * The falling edge did not become a stable press, so treat it as bounce
         * and re-arm the GPIO interrupt.
         */
        bDebounceActive = false;
        EnableSw1Interrupt();
        return EVT_NONE;
    }

    if(!bSamplePressed)
    {
        /*
         * After a reported press, wait for a stable release before re-enabling
         * the falling-edge interrupt for the next user press.
         */
        bDebounceActive = false;
        bPressReported = false;
        EnableSw1Interrupt();
    }

    return EVT_NONE;
}
