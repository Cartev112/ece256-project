#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "dma.h"
#include "music.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_nvic.h"
#include "inc/hw_pwm.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_timer.h"
#include "inc/hw_types.h"
#include "inc/hw_udma.h"

#define DMA_AUDIO_SAMPLE_RATE_HZ     8000U
#define DMA_AUDIO_BUFFER_SAMPLES     128U
#define DMA_AUDIO_PWM_LOAD           255U
#define DMA_AUDIO_PWM_SILENCE        128U
#define DMA_AUDIO_PWM_LOW            64U
#define DMA_AUDIO_PWM_HIGH           192U

#define UDMA_TIMER0A_CHANNEL         18U
#define UDMA_TIMER0A_CHANNEL_BIT     (1UL << UDMA_TIMER0A_CHANNEL)
#define UDMA_TIMER0A_ALT_INDEX       (UDMA_TIMER0A_CHANNEL + 32U)
#define TIMER0A_NVIC_ENABLE_BIT      (1UL << (INT_TIMER0A - 16U))
#define UDMAERR_NVIC_ENABLE_BIT      (1UL << (INT_UDMAERR - 48U))

typedef struct
{
    void *pvSrcEndAddr;
    void *pvDstEndAddr;
    uint32_t ui32Control;
    uint32_t ui32Spare;
} dma_control_entry_t;

#if defined(__clang__)
static volatile dma_control_entry_t g_sDmaControlTable[64]
    __attribute__((aligned(1024)));
#else
#pragma DATA_ALIGN(g_sDmaControlTable, 1024)
static volatile dma_control_entry_t g_sDmaControlTable[64];
#endif

/*
 * The uDMA engine reads these two buffers directly. Each buffer contains one
 * PWM compare value per audio sample. Timer0A clocks the DMA request at 8 kHz,
 * and each request copies one word into PWM0 generator 0 CMPA.
 */
static uint32_t g_ui32PrimaryBuffer[DMA_AUDIO_BUFFER_SAMPLES];
static uint32_t g_ui32AlternateBuffer[DMA_AUDIO_BUFFER_SAMPLES];
static volatile bool g_bDmaAudioInitialized = false;
static volatile bool g_bDmaAudioRunning = false;
static volatile uint32_t g_ui32PhaseAccumulator = 0U;
static volatile uint32_t g_ui32PhaseStep = 0U;
static volatile uint16_t g_ui16ActiveFrequencyHz = 0U;
static volatile dma_audio_status_t g_sDmaAudioStatus = { 0U };

static uint32_t
BuildControlWord(void)
{
    /*
     * One ping-pong descriptor moves the whole buffer. The destination never
     * increments because every sample writes the same hardware register:
     * PWM0 generator 0 CMPA.
     */
    return(UDMA_CHCTL_DSTINC_NONE |
           UDMA_CHCTL_DSTSIZE_32 |
           UDMA_CHCTL_SRCINC_32 |
           UDMA_CHCTL_SRCSIZE_32 |
           UDMA_CHCTL_ARBSIZE_1 |
           (((uint32_t)DMA_AUDIO_BUFFER_SAMPLES - 1U) <<
            UDMA_CHCTL_XFERSIZE_S) |
           UDMA_CHCTL_XFERMODE_PINGPONG);
}

static void
FillAudioBuffer(uint32_t *pui32Buffer)
{
    uint32_t ui32Index;
    uint32_t ui32Duty;
    uint32_t ui32Phase;

    ui32Phase = g_ui32PhaseAccumulator;

    for(ui32Index = 0U; ui32Index < DMA_AUDIO_BUFFER_SAMPLES; ui32Index++)
    {
        /*
         * This is intentionally a simple square wave. DMA proves useful here
         * because it streams many compare updates per second, while the FSM
         * still controls which note should currently be generated.
         */
        if((g_ui16ActiveFrequencyHz == NOTE_REST_HZ) ||
           (g_ui32PhaseStep == 0U))
        {
            ui32Duty = DMA_AUDIO_PWM_SILENCE;
        }
        else
        {
            ui32Phase += g_ui32PhaseStep;
            ui32Duty = ((ui32Phase & 0x80000000UL) != 0U) ?
                       DMA_AUDIO_PWM_HIGH : DMA_AUDIO_PWM_LOW;
        }

        pui32Buffer[ui32Index] = ui32Duty;
    }

    g_ui32PhaseAccumulator = ui32Phase;
    g_sDmaAudioStatus.buffer_refill_count++;
}

static void
ConfigureDescriptor(uint32_t ui32TableIndex, uint32_t *pui32Buffer)
{
    /*
     * uDMA descriptors use end addresses, not start addresses. The controller
     * walks backward internally based on the source increment and transfer
     * size fields in the control word.
     */
    g_sDmaControlTable[ui32TableIndex].pvSrcEndAddr =
        &pui32Buffer[DMA_AUDIO_BUFFER_SAMPLES - 1U];
    g_sDmaControlTable[ui32TableIndex].pvDstEndAddr =
        (void *)(PWM0_BASE + PWM_O_0_CMPA);
    g_sDmaControlTable[ui32TableIndex].ui32Control = BuildControlWord();
    g_sDmaControlTable[ui32TableIndex].ui32Spare = 0U;
}

static void
ConfigurePwmCarrier(void)
{
    /*
     * Fixed PWM carrier: 16 MHz / 256 = 62.5 kHz. DMA updates CMPA at the
     * audio sample rate to modulate duty cycle.
     */
    HWREG(PWM0_BASE + PWM_O_0_CTL) &= ~PWM_0_CTL_ENABLE;
    HWREG(PWM0_BASE + PWM_O_0_LOAD) = DMA_AUDIO_PWM_LOAD;
    HWREG(PWM0_BASE + PWM_O_0_CMPA) = DMA_AUDIO_PWM_SILENCE;
    HWREG(PWM0_BASE + PWM_O_0_CTL) |= PWM_0_CTL_ENABLE;
}

static void
ConfigureTimer0A(void)
{
    uint32_t ui32Load;

    ui32Load = (SYSTEM_CLOCK_HZ / DMA_AUDIO_SAMPLE_RATE_HZ) - 1U;

    HWREG(TIMER0_BASE + TIMER_O_CTL) &= ~TIMER_CTL_TAEN;
    HWREG(TIMER0_BASE + TIMER_O_CFG) = TIMER_CFG_32_BIT_TIMER;
    HWREG(TIMER0_BASE + TIMER_O_TAMR) = TIMER_TAMR_TAMR_PERIOD;
    HWREG(TIMER0_BASE + TIMER_O_TAILR) = ui32Load;
    /*
     * Timer timeout is the DMA trigger. SysTick still owns note timing; Timer0A
     * is only the audio sample clock.
     */
    HWREG(TIMER0_BASE + TIMER_O_DMAEV) = TIMER_DMAEV_TATODMAEN;
    HWREG(TIMER0_BASE + TIMER_O_ICR) =
        TIMER_ICR_DMAAINT | TIMER_ICR_TATOCINT;
    HWREG(TIMER0_BASE + TIMER_O_IMR) = TIMER_IMR_DMAAIM;
}

static void
StopDmaHardware(bool bClearFrequency)
{
    if(!g_bDmaAudioInitialized)
    {
        return;
    }

    HWREG(TIMER0_BASE + TIMER_O_CTL) &= ~TIMER_CTL_TAEN;
    HWREG(UDMA_REQMASKSET) = UDMA_TIMER0A_CHANNEL_BIT;
    HWREG(UDMA_ENACLR) = UDMA_TIMER0A_CHANNEL_BIT;
    HWREG(TIMER0_BASE + TIMER_O_ICR) =
        TIMER_ICR_DMAAINT | TIMER_ICR_TATOCINT;

    g_sDmaControlTable[UDMA_TIMER0A_CHANNEL].ui32Control =
        UDMA_CHCTL_XFERMODE_STOP;
    g_sDmaControlTable[UDMA_TIMER0A_ALT_INDEX].ui32Control =
        UDMA_CHCTL_XFERMODE_STOP;

    HWREG(PWM0_BASE + PWM_O_0_CMPA) = DMA_AUDIO_PWM_SILENCE;
    HWREG(PWM0_BASE + PWM_O_ENABLE) &= ~PWM_ENABLE_PWM0EN;

    g_bDmaAudioRunning = false;
    if(bClearFrequency)
    {
        g_ui16ActiveFrequencyHz = 0U;
        g_sDmaAudioStatus.active_frequency_hz = 0U;
    }
}

void
DmaAudioInit(void)
{
    if(g_bDmaAudioInitialized)
    {
        return;
    }

    HWREG(SYSCTL_RCGCDMA) |= SYSCTL_RCGCDMA_R0;
    HWREG(SYSCTL_RCGCTIMER) |= SYSCTL_RCGCTIMER_R0;
    while((HWREG(SYSCTL_PRDMA) & SYSCTL_PRDMA_R0) == 0U)
    {
    }
    while((HWREG(SYSCTL_PRTIMER) & SYSCTL_PRTIMER_R0) == 0U)
    {
    }

    ConfigurePwmCarrier();
    ConfigureTimer0A();

    HWREG(UDMA_CFG) = UDMA_CFG_MASTEN;
    HWREG(UDMA_CTLBASE) =
        ((uint32_t)g_sDmaControlTable) & UDMA_CTLBASE_ADDR_M;

    /*
     * Channel 18 defaults to Timer0A on TM4C123. Clear its map field to keep
     * that default selected explicitly.
     */
    HWREG(UDMA_CHMAP2) &= ~UDMA_CHMAP2_CH18SEL_M;
    HWREG(UDMA_USEBURSTCLR) = UDMA_TIMER0A_CHANNEL_BIT;
    HWREG(UDMA_REQMASKSET) = UDMA_TIMER0A_CHANNEL_BIT;
    HWREG(UDMA_ENACLR) = UDMA_TIMER0A_CHANNEL_BIT;
    HWREG(UDMA_ALTCLR) = UDMA_TIMER0A_CHANNEL_BIT;
    HWREG(UDMA_PRIOCLR) = UDMA_TIMER0A_CHANNEL_BIT;

    HWREG(NVIC_EN0) |= TIMER0A_NVIC_ENABLE_BIT;
    HWREG(NVIC_EN1) |= UDMAERR_NVIC_ENABLE_BIT;

    g_bDmaAudioInitialized = true;
}

void
DmaAudioStop(void)
{
    StopDmaHardware(true);
}

void
DmaAudioStartNote(uint16_t ui16FrequencyHz)
{
    uint64_t ui64PhaseStep;

    DmaAudioInit();
    DmaAudioStop();

    if(ui16FrequencyHz == NOTE_REST_HZ)
    {
        return;
    }

    g_ui16ActiveFrequencyHz = ui16FrequencyHz;
    g_sDmaAudioStatus.active_frequency_hz = ui16FrequencyHz;
    g_ui32PhaseAccumulator = 0U;
    ui64PhaseStep = ((uint64_t)ui16FrequencyHz << 32) /
                    DMA_AUDIO_SAMPLE_RATE_HZ;
    g_ui32PhaseStep = (uint32_t)ui64PhaseStep;

    FillAudioBuffer(g_ui32PrimaryBuffer);
    FillAudioBuffer(g_ui32AlternateBuffer);
    ConfigureDescriptor(UDMA_TIMER0A_CHANNEL, g_ui32PrimaryBuffer);
    ConfigureDescriptor(UDMA_TIMER0A_ALT_INDEX, g_ui32AlternateBuffer);

    ConfigurePwmCarrier();
    HWREG(PWM0_BASE + PWM_O_ENABLE) |= PWM_ENABLE_PWM0EN;
    HWREG(UDMA_CHIS) = UDMA_TIMER0A_CHANNEL_BIT;
    HWREG(UDMA_REQMASKCLR) = UDMA_TIMER0A_CHANNEL_BIT;
    HWREG(UDMA_ENASET) = UDMA_TIMER0A_CHANNEL_BIT;
    g_bDmaAudioRunning = true;
    HWREG(TIMER0_BASE + TIMER_O_CTL) |= TIMER_CTL_TAEN;
}

void
DmaAudioPause(void)
{
    StopDmaHardware(false);
}

void
DmaAudioResume(void)
{
    if(g_ui16ActiveFrequencyHz != NOTE_REST_HZ)
    {
        DmaAudioStartNote(g_ui16ActiveFrequencyHz);
    }
}

void
DmaAudioGetStatus(dma_audio_status_t *psStatus)
{
    if(psStatus == 0)
    {
        return;
    }

    *psStatus = g_sDmaAudioStatus;
}

void
Timer0A_Handler(void)
{
    uint32_t ui32PrimaryMode;
    uint32_t ui32AlternateMode;
    bool bRearmed = false;

    HWREG(TIMER0_BASE + TIMER_O_ICR) =
        TIMER_ICR_DMAAINT | TIMER_ICR_TATOCINT;
    HWREG(UDMA_CHIS) = UDMA_TIMER0A_CHANNEL_BIT;
    g_sDmaAudioStatus.timer_dma_irq_count++;

    if(!g_bDmaAudioRunning)
    {
        return;
    }

    ui32PrimaryMode =
        g_sDmaControlTable[UDMA_TIMER0A_CHANNEL].ui32Control &
        UDMA_CHCTL_XFERMODE_M;
    ui32AlternateMode =
        g_sDmaControlTable[UDMA_TIMER0A_ALT_INDEX].ui32Control &
        UDMA_CHCTL_XFERMODE_M;

    if(ui32PrimaryMode == UDMA_CHCTL_XFERMODE_STOP)
    {
        /*
         * Hardware marks a descriptor STOP when that half of the ping-pong
         * transfer is complete. Refill only that completed buffer and re-arm
         * its descriptor; the other half may still be actively feeding PWM.
         */
        FillAudioBuffer(g_ui32PrimaryBuffer);
        ConfigureDescriptor(UDMA_TIMER0A_CHANNEL, g_ui32PrimaryBuffer);
        g_sDmaAudioStatus.primary_done_count++;
        bRearmed = true;
    }

    if(ui32AlternateMode == UDMA_CHCTL_XFERMODE_STOP)
    {
        /*
         * The alternate descriptor is the second half of the ping-pong pair.
         * Keeping primary/alternate counters separate makes board testing much
         * easier over UART.
         */
        FillAudioBuffer(g_ui32AlternateBuffer);
        ConfigureDescriptor(UDMA_TIMER0A_ALT_INDEX, g_ui32AlternateBuffer);
        g_sDmaAudioStatus.alternate_done_count++;
        bRearmed = true;
    }

    if((ui32PrimaryMode == UDMA_CHCTL_XFERMODE_STOP) &&
       (ui32AlternateMode == UDMA_CHCTL_XFERMODE_STOP))
    {
        g_sDmaAudioStatus.underrun_count++;
    }

    if(!bRearmed)
    {
        g_sDmaAudioStatus.bad_isr_count++;
    }

    HWREG(UDMA_ENASET) = UDMA_TIMER0A_CHANNEL_BIT;
}

void
uDMAError_Handler(void)
{
    if((HWREG(UDMA_ERRCLR) & UDMA_ERRCLR_ERRCLR) != 0U)
    {
        HWREG(UDMA_ERRCLR) = UDMA_ERRCLR_ERRCLR;
        g_sDmaAudioStatus.dma_error_count++;
    }
}
