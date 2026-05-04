//*****************************************************************************
//
// blinky.c - Top-level loop for the FSM-driven TM4C music player.
//
//*****************************************************************************

#include "app_types.h"
#include "board.h"
#include "button.h"
#include "commands.h"
#include "fsm.h"
#include "systick.h"
#include "uart.h"

int
main(void)
{
    app_context_t sAppContext;
    char cRxChar;

    BoardInit();
    ButtonInit();
    UartInit();
    SysTickInit();
    FsmInit(&sAppContext);

    UartWriteString("UART0 ready at 115200 8-N-1, RX interrupt enabled\n");
    UartWriteString("Commands: p=play/resume a=pause s=stop r=restart h=?=help\n");
    UartWriteStatus(&sAppContext, "STATE");

    while(1)
    {
        while(UartReadChar(&cRxChar))
        {
            HandleUartCommand(&sAppContext, cRxChar);
        }

        if(UartConsumeRxOverflow())
        {
            UartWriteString("UART RX overflow: command byte dropped\n");
        }

        /*
         * SysTick provides the time base. The main loop does the actual work
         * once per tick so the ISRs stay short and predictable.
         */
        if(SysTickConsumeElapsed())
        {
            FsmHandleEvent(&sAppContext, ButtonProcessEvent());
            FsmHandleEvent(&sAppContext, FsmGenerateTimedEvent(&sAppContext));
        }
    }
}
