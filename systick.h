#ifndef SYSTICK_H_
#define SYSTICK_H_

#include <stdbool.h>
#include <stdint.h>

void SysTickInit(void);
bool SysTickConsumeElapsed(void);
uint32_t SysTickGetTicks(void);
void SysTick_Handler(void);

#endif
