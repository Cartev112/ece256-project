#ifndef BUTTON_H_
#define BUTTON_H_

#include "app_types.h"

void ButtonInit(void);
app_event_t ButtonProcessEvent(void);
void GPIOF_Handler(void);

#endif
