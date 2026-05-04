#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "app_types.h"
#include "board_pins.h"
#include "systick.h"
#include "uart.h"
#include "inc/hw_gpio.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_nvic.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"
#include "inc/hw_uart.h"

#define UART_RX_BUFFER_SIZE         32U
#define UART_RX_BUFFER_MASK         (UART_RX_BUFFER_SIZE - 1U)
#define UART_RX_INTERRUPT_MASK      (UART_IM_RXIM | UART_IM_RTIM)
#define UART_ERROR_INTERRUPT_MASK   (UART_IM_OEIM | UART_IM_BEIM | \
                                     UART_IM_PEIM | UART_IM_FEIM)
#define UART0_NVIC_ENABLE_BIT       (1U << (INT_UART0 - 16U))

static volatile char g_pcUartRxBuffer[UART_RX_BUFFER_SIZE];
static volatile uint8_t g_ui8UartRxHead = 0U;
static volatile uint8_t g_ui8UartRxTail = 0U;
static volatile bool g_bUartRxOverflow = false;

void
UartInit(void)
{
    /*
     * UART0 uses PA0/PA1. With a 16 MHz system clock and 115200 baud:
     * BRD = 16,000,000 / (16 * 115200) = 8.6805, so IBRD=8, FBRD=44.
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
    HWREG(UART0_BASE + UART_O_IM) = 0U;
    HWREG(UART0_BASE + UART_O_IBRD) = 8U;
    HWREG(UART0_BASE + UART_O_FBRD) = 44U;
    HWREG(UART0_BASE + UART_O_LCRH) = UART_LCRH_WLEN_8 | UART_LCRH_FEN;
    HWREG(UART0_BASE + UART_O_IFLS) =
        (HWREG(UART0_BASE + UART_O_IFLS) & ~UART_IFLS_RX_M) |
        UART_IFLS_RX1_8;
    HWREG(UART0_BASE + UART_O_ICR) =
        UART_ICR_RXIC | UART_ICR_RTIC | UART_ICR_OEIC | UART_ICR_BEIC |
        UART_ICR_PEIC | UART_ICR_FEIC;
    HWREG(UART0_BASE + UART_O_IM) =
        UART_RX_INTERRUPT_MASK | UART_ERROR_INTERRUPT_MASK;
    HWREG(UART0_BASE + UART_O_CTL) =
        UART_CTL_UARTEN | UART_CTL_TXE | UART_CTL_RXE;

    HWREG(NVIC_EN0) |= UART0_NVIC_ENABLE_BIT;
}

void
UartWriteChar(char cChar)
{
    while((HWREG(UART0_BASE + UART_O_FR) & UART_FR_TXFF) != 0U)
    {
    }

    HWREG(UART0_BASE + UART_O_DR) = (uint32_t)cChar;
}

void
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

void
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

bool
UartReadChar(char *pcChar)
{
    uint8_t ui8Tail;

    ui8Tail = g_ui8UartRxTail;
    if(ui8Tail == g_ui8UartRxHead)
    {
        return false;
    }

    *pcChar = g_pcUartRxBuffer[ui8Tail];
    g_ui8UartRxTail = (uint8_t)((ui8Tail + 1U) & UART_RX_BUFFER_MASK);
    return true;
}

bool
UartConsumeRxOverflow(void)
{
    if(!g_bUartRxOverflow)
    {
        return false;
    }

    g_bUartRxOverflow = false;
    return true;
}

void
UART0_Handler(void)
{
    uint32_t ui32Status;
    uint8_t ui8NextHead;
    char cRxChar;

    ui32Status = HWREG(UART0_BASE + UART_O_MIS);

    if((ui32Status & UART_ERROR_INTERRUPT_MASK) != 0U)
    {
        HWREG(UART0_BASE + UART_O_ICR) =
            ui32Status & UART_ERROR_INTERRUPT_MASK;
    }

    if((ui32Status & (UART_MIS_RXMIS | UART_MIS_RTMIS)) != 0U)
    {
        while((HWREG(UART0_BASE + UART_O_FR) & UART_FR_RXFE) == 0U)
        {
            cRxChar = (char)(HWREG(UART0_BASE + UART_O_DR) & UART_DR_DATA_M);
            ui8NextHead =
                (uint8_t)((g_ui8UartRxHead + 1U) & UART_RX_BUFFER_MASK);

            if(ui8NextHead == g_ui8UartRxTail)
            {
                g_bUartRxOverflow = true;
            }
            else
            {
                g_pcUartRxBuffer[g_ui8UartRxHead] = cRxChar;
                g_ui8UartRxHead = ui8NextHead;
            }
        }

        HWREG(UART0_BASE + UART_O_ICR) = UART_ICR_RXIC | UART_ICR_RTIC;
    }
}

void
UartWriteStatus(const app_context_t *psContext, const char *pcPrefix)
{
    UartWriteString(pcPrefix);
    UartWriteString(": tick=");
    UartWriteUInt(SysTickGetTicks());
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
