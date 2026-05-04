#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "systick.h"
#include "inc/hw_nvic.h"
#include "inc/hw_types.h"

static volatile bool g_bSysTickElapsed = false;
static volatile uint32_t g_ui32SystemTicks = 0U;

void
SysTick_Handler(void)
{
    /*
     * Keep the ISR short. It only records that one scheduler tick elapsed; the
     * main loop handles debounce, FSM transitions, LED updates, and audio.
     */
    g_ui32SystemTicks++;
    g_bSysTickElapsed = true;
}

void
SysTickInit(void)
{
    uint32_t ui32Reload;

    ui32Reload = ((SYSTEM_CLOCK_HZ / 1000U) * SYSTICK_PERIOD_MS) - 1U;

    HWREG(NVIC_ST_CTRL) = 0U;
    HWREG(NVIC_ST_RELOAD) = ui32Reload & NVIC_ST_RELOAD_M;
    HWREG(NVIC_ST_CURRENT) = 0U;
    HWREG(NVIC_ST_CTRL) =
        NVIC_ST_CTRL_CLK_SRC | NVIC_ST_CTRL_INTEN | NVIC_ST_CTRL_ENABLE;
}

bool
SysTickConsumeElapsed(void)
{
    if(!g_bSysTickElapsed)
    {
        return false;
    }

    g_bSysTickElapsed = false;
    return true;
}

uint32_t
SysTickGetTicks(void)
{
    return g_ui32SystemTicks;
}
