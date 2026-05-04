#include <stdint.h>
#include "board.h"
#include "board_pins.h"
#include "inc/hw_gpio.h"
#include "inc/hw_memmap.h"
#include "inc/hw_pwm.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"

void
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
    HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) =
        (HWREG(GPIO_PORTF_BASE + GPIO_O_DIR) & ~SW1_PIN) | RGB_LED_PINS;
    HWREG(GPIO_PORTF_BASE + GPIO_O_PUR) |= SW1_PIN;
    HWREG(GPIO_PORTF_BASE + GPIO_O_DEN) |= (RGB_LED_PINS | SW1_PIN);
    HWREG(GPIO_PORTF_BASE + (GPIO_O_DATA + (RGB_LED_PINS << 2))) = 0U;

    /*
     * PB0-PB3 are plain digital outputs. PB6 is assigned to M0PWM0.
     */
    HWREG(GPIO_PORTB_BASE + GPIO_O_AFSEL) &= ~PORTB_LED_PINS;
    HWREG(GPIO_PORTB_BASE + GPIO_O_AFSEL) |= AUDIO_PWM_PIN;
    HWREG(GPIO_PORTB_BASE + GPIO_O_AMSEL) &= ~(PORTB_LED_PINS | AUDIO_PWM_PIN);
    HWREG(GPIO_PORTB_BASE + GPIO_O_PCTL) =
        (HWREG(GPIO_PORTB_BASE + GPIO_O_PCTL) & ~0x0F00000FU) |
        GPIO_PCTL_PB6_M0PWM0_VALUE;
    HWREG(GPIO_PORTB_BASE + GPIO_O_DIR) |= PORTB_LED_PINS;
    HWREG(GPIO_PORTB_BASE + GPIO_O_DEN) |= (PORTB_LED_PINS | AUDIO_PWM_PIN);
    HWREG(GPIO_PORTB_BASE + (GPIO_O_DATA + (PORTB_LED_PINS << 2))) = 0U;

    /*
     * PWM generator 0 controls M0PWM0. The generator action drives the output
     * high at LOAD and low at comparator A, producing a 50% duty square wave.
     */
    HWREG(PWM0_BASE + PWM_O_0_CTL) = 0U;
    HWREG(PWM0_BASE + PWM_O_0_GENA) =
        PWM_0_GENA_ACTLOAD_ONE | PWM_0_GENA_ACTCMPAD_ZERO;
    HWREG(PWM0_BASE + PWM_O_ENABLE) &= ~PWM_ENABLE_PWM0EN;
}
