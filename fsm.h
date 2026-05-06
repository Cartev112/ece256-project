#ifndef FSM_H_
#define FSM_H_

#include "app_types.h"

void FsmInit(app_context_t *psContext);
void FsmHandleEvent(app_context_t *psContext, app_event_t eEvent);
app_event_t FsmGenerateTimedEvent(app_context_t *psContext);

#endif
