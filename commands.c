#include "app_types.h"
#include "commands.h"
#include "dma.h"
#include "fsm.h"
#include "uart.h"

static void
WriteDmaStatus(void)
{
    dma_audio_status_t sStatus;

    DmaAudioGetStatus(&sStatus);
    UartWriteString("DMA: primary=");
    UartWriteUInt(sStatus.primary_done_count);
    UartWriteString(" alternate=");
    UartWriteUInt(sStatus.alternate_done_count);
    UartWriteString(" irq=");
    UartWriteUInt(sStatus.timer_dma_irq_count);
    UartWriteString(" refill=");
    UartWriteUInt(sStatus.buffer_refill_count);
    UartWriteString(" underrun=");
    UartWriteUInt(sStatus.underrun_count);
    UartWriteString(" errors=");
    UartWriteUInt(sStatus.dma_error_count);
    UartWriteString(" bad_isr=");
    UartWriteUInt(sStatus.bad_isr_count);
    UartWriteString(" freq=");
    UartWriteUInt(sStatus.active_frequency_hz);
    UartWriteChar('\n');
}

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

        case 'd':
            WriteDmaStatus();
            break;

        case 'h':
        case '?':
            UartWriteString("Commands: p=play/resume a=pause s=stop r=restart d=dma h=?=help\n");
            UartWriteStatus(psContext, "STATUS");
            WriteDmaStatus();
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
