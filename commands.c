#include "app_types.h"
#include "commands.h"
#include "fsm.h"
#include "uart.h"

void
HandleUartCommand(app_context_t *psContext, char cCommand)
{
    if((cCommand >= 'A') && (cCommand <= 'Z'))
    {
        cCommand = (char)(cCommand - 'A' + 'a');
    }

    switch(cCommand)
    {
        case 'p':
            FsmHandleEvent(psContext, EVT_UART_PLAY);
            break;

        case 'a':
            FsmHandleEvent(psContext, EVT_UART_PAUSE);
            break;

        case 's':
            FsmHandleEvent(psContext, EVT_UART_STOP);
            break;

        case 'r':
            FsmHandleEvent(psContext, EVT_UART_RESTART);
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
