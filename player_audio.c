#include <stdint.h>
#include "app_config.h"
#include "music.h"
#include "player_audio.h"
#include "inc/hw_memmap.h"
#include "inc/hw_pwm.h"
#include "inc/hw_types.h"

void
AudioStop(void)
{
    HWREG(PWM0_BASE + PWM_O_ENABLE) &= ~PWM_ENABLE_PWM0EN;
    HWREG(PWM0_BASE + PWM_O_0_CTL) &= ~PWM_0_CTL_ENABLE;
}

void
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
