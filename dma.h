#ifndef DMA_H_
#define DMA_H_

#include <stdint.h>

typedef struct
{
    uint32_t primary_done_count;
    uint32_t alternate_done_count;
    uint32_t timer_dma_irq_count;
    uint32_t buffer_refill_count;
    uint32_t underrun_count;
    uint32_t dma_error_count;
    uint32_t bad_isr_count;
    uint32_t active_frequency_hz;
} dma_audio_status_t;

void DmaAudioInit(void);
void DmaAudioStartNote(uint16_t ui16FrequencyHz);
void DmaAudioStop(void);
void DmaAudioPause(void);
void DmaAudioResume(void);
void DmaAudioGetStatus(dma_audio_status_t *psStatus);
void Timer0A_Handler(void);
void uDMAError_Handler(void);

#endif
