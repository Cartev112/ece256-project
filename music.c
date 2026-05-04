#include "music.h"

#define QUARTER_NOTE_MS             400U
#define HALF_NOTE_MS                800U

/*
 * Twinkle Twinkle Little Star in C major.
 *
 * The phrase IDs follow the common line structure:
 *   0: C C G G A A G
 *   1: F F E E D D C
 *   2: G G F F E E D
 *   3: G G F F E E D
 *   4: C C G G A A G
 *   5: F F E E D D C
 */
const note_t g_twinkle_song[] =
{
    { NOTE_C4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_C4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_A4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_A4_HZ, QUARTER_NOTE_MS, 0U },
    { NOTE_G4_HZ, HALF_NOTE_MS,    0U },

    { NOTE_F4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_D4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_D4_HZ, QUARTER_NOTE_MS, 1U },
    { NOTE_C4_HZ, HALF_NOTE_MS,    1U },

    { NOTE_G4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 2U },
    { NOTE_D4_HZ, HALF_NOTE_MS,    2U },

    { NOTE_G4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 3U },
    { NOTE_D4_HZ, HALF_NOTE_MS,    3U },

    { NOTE_C4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_C4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_G4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_A4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_A4_HZ, QUARTER_NOTE_MS, 4U },
    { NOTE_G4_HZ, HALF_NOTE_MS,    4U },

    { NOTE_F4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_F4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_E4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_D4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_D4_HZ, QUARTER_NOTE_MS, 5U },
    { NOTE_C4_HZ, HALF_NOTE_MS,    5U }
};

const uint16_t g_ui16TwinkleNoteCount =
    (uint16_t)(sizeof(g_twinkle_song) / sizeof(g_twinkle_song[0]));
