#ifndef APP_TYPES_H_
#define APP_TYPES_H_

#include <stdint.h>

/*
 * Expanded playback state list. LOAD_PHRASE, LOAD_NOTE, NOTE_ON, and NOTE_GAP
 * together form the active playback region required by the assignment.
 */
typedef enum
{
    STATE_IDLE = 0,
    STATE_LOAD_PHRASE,
    STATE_LOAD_NOTE,
    STATE_NOTE_ON,
    STATE_NOTE_GAP,
    STATE_PAUSED,
    STATE_SONG_COMPLETE
} app_state_t;

/*
 * FSM input events. Hardware and timing modules translate raw conditions into
 * these events so the FSM does not depend directly on peripheral registers.
 */
typedef enum
{
    EVT_NONE = 0,
    EVT_SW1_PRESS,
    EVT_UART_PLAY,
    EVT_UART_PAUSE,
    EVT_UART_STOP,
    EVT_UART_RESTART,
    EVT_PHRASE_READY,
    EVT_NOTE_READY,
    EVT_NOTE_DONE,
    EVT_GAP_DONE,
    EVT_PHRASE_DONE,
    EVT_SONG_DONE
} app_event_t;

typedef struct
{
    app_state_t state;
    app_state_t resume_state;
    uint16_t song_index;
    uint8_t phrase_index;
    uint16_t state_ticks;
    uint16_t resume_ticks;
} app_context_t;

#endif
