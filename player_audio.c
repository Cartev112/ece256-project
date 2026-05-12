#include <stdint.h>
#include "dma.h"
#include "music.h"
#include "player_audio.h"

void
AudioStop(void)
{
    DmaAudioStop();
}

void
AudioStart(uint16_t ui16FrequencyHz)
{
    if(ui16FrequencyHz == NOTE_REST_HZ)
    {
        AudioStop();
        return;
    }

    DmaAudioStartNote(ui16FrequencyHz);
}
