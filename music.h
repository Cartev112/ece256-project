#ifndef MUSIC_H_
#define MUSIC_H_

#include <stdint.h>

#define NOTE_REST_HZ                0U
#define NOTE_C4_HZ                  262U
#define NOTE_D4_HZ                  294U
#define NOTE_E4_HZ                  330U
#define NOTE_F4_HZ                  349U
#define NOTE_G4_HZ                  392U
#define NOTE_A4_HZ                  440U

typedef struct
{
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint8_t phrase_id;
} note_t;

extern const note_t g_twinkle_song[];
extern const uint16_t g_ui16TwinkleNoteCount;

#endif
