#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "app_types.h"
#include "fsm.h"
#include "led.h"
#include "music.h"
#include "player_audio.h"
#include "uart.h"

typedef struct
{
    app_state_t current_state;
    app_event_t event;
    app_state_t next_state;
} fsm_transition_t;

static const fsm_transition_t g_fsm_transitions[] =
{
    { STATE_IDLE,          EVT_SW1_PRESS,    STATE_LOAD_PHRASE   },
    { STATE_LOAD_PHRASE,   EVT_PHRASE_READY, STATE_LOAD_NOTE     },
    { STATE_LOAD_NOTE,     EVT_NOTE_READY,   STATE_NOTE_ON       },
    { STATE_NOTE_ON,       EVT_NOTE_DONE,    STATE_NOTE_GAP      },
    { STATE_NOTE_GAP,      EVT_GAP_DONE,     STATE_LOAD_NOTE     },
    { STATE_NOTE_GAP,      EVT_PHRASE_DONE,  STATE_LOAD_PHRASE   },
    { STATE_NOTE_GAP,      EVT_SONG_DONE,    STATE_SONG_COMPLETE },
    { STATE_SONG_COMPLETE, EVT_SW1_PRESS,    STATE_LOAD_PHRASE   }
};

static uint16_t
MsToTicks(uint16_t ui16DurationMs)
{
    uint16_t ui16Ticks;

    ui16Ticks = (uint16_t)(ui16DurationMs / SYSTICK_PERIOD_MS);
    if(ui16Ticks == 0U)
    {
        ui16Ticks = 1U;
    }

    return ui16Ticks;
}

static bool
IsActivePlaybackState(app_state_t eState)
{
    return((eState == STATE_LOAD_PHRASE) ||
           (eState == STATE_LOAD_NOTE) ||
           (eState == STATE_NOTE_ON) ||
           (eState == STATE_NOTE_GAP));
}

static void
ResetPlaybackPosition(app_context_t *psContext)
{
    psContext->song_index = 0U;
    psContext->phrase_index = g_twinkle_song[0].phrase_id;
    psContext->resume_state = STATE_IDLE;
    psContext->resume_ticks = 0U;
}

static void
EnterState(app_context_t *psContext, app_state_t eNextState)
{
    psContext->state = eNextState;
    psContext->state_ticks = 0U;

    switch(eNextState)
    {
        case STATE_IDLE:
        case STATE_LOAD_PHRASE:
        case STATE_LOAD_NOTE:
        case STATE_NOTE_GAP:
        case STATE_PAUSED:
        case STATE_SONG_COMPLETE:
            AudioStop();
            break;

        case STATE_NOTE_ON:
            if(psContext->song_index < g_ui16TwinkleNoteCount)
            {
                AudioStart(g_twinkle_song[psContext->song_index].frequency_hz);
            }
            else
            {
                AudioStop();
            }
            break;

        default:
            AudioStop();
            break;
    }

    LedApplyStateOutputs(psContext);
    UartWriteStatus(psContext, "STATE");
}

static void
ApplyTransitionAction(app_context_t *psContext, app_event_t eEvent)
{
    switch(eEvent)
    {
        case EVT_SW1_PRESS:
            if((psContext->state == STATE_IDLE) ||
               (psContext->state == STATE_SONG_COMPLETE))
            {
                ResetPlaybackPosition(psContext);
            }
            break;

        case EVT_GAP_DONE:
        case EVT_PHRASE_DONE:
            psContext->song_index++;
            psContext->phrase_index =
                g_twinkle_song[psContext->song_index].phrase_id;
            break;

        case EVT_SONG_DONE:
            psContext->song_index = g_ui16TwinkleNoteCount;
            break;

        default:
            break;
    }
}

void
FsmInit(app_context_t *psContext)
{
    psContext->state = STATE_IDLE;
    psContext->resume_state = STATE_IDLE;
    psContext->song_index = 0U;
    psContext->phrase_index = 0U;
    psContext->state_ticks = 0U;
    psContext->resume_ticks = 0U;
    LedApplyStateOutputs(psContext);
}

void
FsmHandleEvent(app_context_t *psContext, app_event_t eEvent)
{
    uint32_t ui32Index;

    if(eEvent == EVT_NONE)
    {
        return;
    }

    /*
     * UART control events intentionally map onto the same user-visible
     * behavior as SW1. The UART path never directly manipulates audio or LEDs.
     */
    if(eEvent == EVT_UART_PLAY)
    {
        if((psContext->state == STATE_IDLE) ||
           (psContext->state == STATE_PAUSED) ||
           (psContext->state == STATE_SONG_COMPLETE))
        {
            FsmHandleEvent(psContext, EVT_SW1_PRESS);
        }
        return;
    }

    if(eEvent == EVT_UART_PAUSE)
    {
        if(IsActivePlaybackState(psContext->state))
        {
            FsmHandleEvent(psContext, EVT_SW1_PRESS);
        }
        return;
    }

    if(eEvent == EVT_UART_STOP)
    {
        ResetPlaybackPosition(psContext);
        EnterState(psContext, STATE_IDLE);
        return;
    }

    if(eEvent == EVT_UART_RESTART)
    {
        ResetPlaybackPosition(psContext);
        EnterState(psContext, STATE_LOAD_PHRASE);
        return;
    }

    if((eEvent == EVT_SW1_PRESS) && IsActivePlaybackState(psContext->state))
    {
        psContext->resume_state = psContext->state;
        psContext->resume_ticks = psContext->state_ticks;
        EnterState(psContext, STATE_PAUSED);
        return;
    }

    if((eEvent == EVT_SW1_PRESS) && (psContext->state == STATE_PAUSED))
    {
        psContext->state = psContext->resume_state;
        psContext->state_ticks = psContext->resume_ticks;
        if((psContext->state == STATE_NOTE_ON) &&
           (psContext->song_index < g_ui16TwinkleNoteCount))
        {
            AudioStart(g_twinkle_song[psContext->song_index].frequency_hz);
        }
        LedApplyStateOutputs(psContext);
        UartWriteStatus(psContext, "STATE");
        return;
    }

    for(ui32Index = 0U;
        ui32Index < (sizeof(g_fsm_transitions) / sizeof(g_fsm_transitions[0]));
        ui32Index++)
    {
        if((g_fsm_transitions[ui32Index].current_state == psContext->state) &&
           (g_fsm_transitions[ui32Index].event == eEvent))
        {
            ApplyTransitionAction(psContext, eEvent);
            EnterState(psContext, g_fsm_transitions[ui32Index].next_state);
            return;
        }
    }
}

app_event_t
FsmGenerateTimedEvent(app_context_t *psContext)
{
    uint16_t ui16NextNote;
    uint16_t ui16NoteTicks;

    if(!IsActivePlaybackState(psContext->state))
    {
        return EVT_NONE;
    }

    psContext->state_ticks++;

    switch(psContext->state)
    {
        case STATE_LOAD_PHRASE:
            if(psContext->state_ticks >= LOAD_PHRASE_TICKS)
            {
                return EVT_PHRASE_READY;
            }
            break;

        case STATE_LOAD_NOTE:
            if(psContext->state_ticks >= LOAD_NOTE_TICKS)
            {
                return EVT_NOTE_READY;
            }
            break;

        case STATE_NOTE_ON:
            if(psContext->song_index >= g_ui16TwinkleNoteCount)
            {
                return EVT_SONG_DONE;
            }

            ui16NoteTicks =
                MsToTicks(g_twinkle_song[psContext->song_index].duration_ms);
            if(psContext->state_ticks >= ui16NoteTicks)
            {
                return EVT_NOTE_DONE;
            }
            break;

        case STATE_NOTE_GAP:
            if(psContext->state_ticks >= NOTE_GAP_TICKS)
            {
                ui16NextNote = psContext->song_index + 1U;

                if(ui16NextNote >= g_ui16TwinkleNoteCount)
                {
                    return EVT_SONG_DONE;
                }

                if(g_twinkle_song[ui16NextNote].phrase_id !=
                   psContext->phrase_index)
                {
                    return EVT_PHRASE_DONE;
                }

                return EVT_GAP_DONE;
            }
            break;

        default:
            break;
    }

    return EVT_NONE;
}
