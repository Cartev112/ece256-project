#ifndef UART_H_
#define UART_H_

#include <stdbool.h>
#include <stdint.h>
#include "app_types.h"

void UartInit(void);
void UartWriteChar(char cChar);
void UartWriteString(const char *pcString);
void UartWriteUInt(uint32_t ui32Value);
bool UartReadChar(char *pcChar);
bool UartConsumeRxOverflow(void);
void UartWriteStatus(const app_context_t *psContext, const char *pcPrefix);
void UART0_Handler(void);

#endif
