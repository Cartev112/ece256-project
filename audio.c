#include <stdint.h>
#include <math.h>

// System control registers
#define SYSCTL_RCGCGPIO_R (*((volatile uint32_t *)0x400FE608)) // GPIO clock
#define SYSCTL_RCGCPWM_R (*((volatile uint32_t *)0x400FE640)) // PWM clock

// GPIO Port B registers (base: 0x40005000) (for GPIO LEDs)
#define GPIO_PORTB_AFSEL_R (*((volatile uint32_t *)0x40005420)) // Alt function
#define GPIO_PORTB_DEN_R (*((volatile uint32_t *)0x4000551C)) // Digital enable
#define GPIO_PORTB_AMSEL_R (*((volatile uint32_t *)0x40005528)) // Analog mode
#define GPIO_PORTB_PCTL_R (*((volatile uint32_t *)0x4000552C)) // Port control
#define GPIO_PORTB_DIR_R (*((volatile uint32_t *) 0x40005400))
#define GPIO_PORTB_DATA_R (*((volatile uint32_t *)0x400053FC)) // Data register  ADDED

// PWM Module 0, Generator 0 registers (base: 0x40028000) (for audio)
#define PWM0_ENABLE_R (*((volatile uint32_t *)0x40028008)) // PWM output enable
#define PWM0_0_CTL_R (*((volatile uint32_t *)0x40028040)) // Generator control
#define PWM0_0_LOAD_R (*((volatile uint32_t *)0x40028050)) // Load (period)
#define PWM0_0_CMPA_R (*((volatile uint32_t *)0x40028058)) // Compare A (duty)
#define PWM0_0_GENA_R (*((volatile uint32_t *)0x40028060)) // Generator A action
#define SYSCLK 16000000 // 16 MHz default clock (no PLL)
#define TONE_HZ 440 // Frequency in Hz
// SysTick registers (Cortex-M core)
#define NVIC_ST_CTRL_R (*((volatile uint32_t *)0xE000E010)) // Control/Status
#define NVIC_ST_RELOAD_R (*((volatile uint32_t *)0xE000E014)) // Reload value
#define NVIC_ST_CURRENT_R (*((volatile uint32_t *)0xE000E018)) // Current value

// Bit masks
#define COUNTFLAG (1U << 16) // Bit 16 of NVIC_ST_CTRL_R

// Register addresses for Port F (base address 0x40025000 + offset) (for sw1 interrupt)
#define GPIO_PORTF_DATA_R (*((volatile uint32_t *)0x400253FC)) // Data register
#define GPIO_PORTF_DIR_R (*((volatile uint32_t *)0x40025400)) // Direction register
#define GPIO_PORTF_AFSEL_R (*((volatile uint32_t *)0x40025420)) // Alternate function
#define SYSCTL_RCGCGPIO_R (*((volatile uint32_t *)0x400FE608)) // Clock control
#define GPIO_PORTF_PUR_R (*((volatile uint32_t *)0x40025510)) // Pull-up
#define GPIO_PORTF_DEN_R (*((volatile uint32_t *)0x4002551C)) // Digital enable
#define GPIO_PORTF_LOCK_R (*((volatile uint32_t *)0x40025520)) // Lock
#define GPIO_PORTF_CR_R (*((volatile uint32_t *)0x40025524)) // Commit
// Port F interrupt registers (for sw1 interrupt)
#define GPIO_PORTF_IS_R (*((volatile uint32_t *)0x40025404)) // Int sense
#define GPIO_PORTF_IBE_R (*((volatile uint32_t *)0x40025408)) // Int both edges
#define GPIO_PORTF_IEV_R (*((volatile uint32_t *)0x4002540C)) // Int event
#define GPIO_PORTF_IM_R (*((volatile uint32_t *)0x40025410)) // Int mask
#define GPIO_PORTF_ICR_R (*((volatile uint32_t *)0x4002541C)) // Int clear
// System control and NVIC
#define NVIC_EN0_R (*((volatile uint32_t *)0xE000E100)) // IRQ 0-31 enable


#define SW1 0x10

//FSM implementation
typedef enum
{
    EVT_NONE = 0,
    EVT_SW1_PRESS,
    EVT_PHRASE_READY,
    EVT_NOTE_READY,
    EVT_NOTE_DONE,
    EVT_GAP_DONE,
    EVT_PHRASE_DONE,
    EVT_SONG_DONE
} app_event_t;

app_event_t state = EVT_NONE;

/* port initializations:
   F for   SW1 interrupt
   B 0-3   for GPIO LED output
   B 6     for PWM
   this is an absolute redundant mess and will be tidied up later
*/
void Board_Init(void) {
SYSCTL_RCGCGPIO_R |= 0x20; // Enable clock for Port F
while ((SYSCTL_RCGCGPIO_R & 0x20) == 0) {} // Wait for clock
    GPIO_PORTF_LOCK_R = 0x4C4F434B; // Unlock Port F
    GPIO_PORTF_CR_R = 0x1F; // Allow changes to PF4-PF0
    GPIO_PORTF_DIR_R = 0x0E; // PF1-PF3 output, PF4,PF0 input
    GPIO_PORTF_AFSEL_R = 0x00; // Disable alternate functions
    GPIO_PORTF_PUR_R = 0x11; // Enable pull-up on PF4 and PF0
    GPIO_PORTF_DEN_R = 0x1F; // Enable digital I/O on PF4-PF0
    GPIO_PORTF_IS_R &= ~SW1; // Edge-triggered
    GPIO_PORTF_IBE_R |= SW1; // Both edges
    GPIO_PORTF_ICR_R = SW1; // Clear pending interrupt
    GPIO_PORTF_IM_R |= SW1; // Enable interrupt on SW1
    NVIC_EN0_R |= (1 << 30); // Enable Port F in NVIC
    // start of PWM init
    SYSCTL_RCGCGPIO_R |= 0x02; // Enable clock for Port B
    SYSCTL_RCGCPWM_R |= 0x01; // Enable clock for PWM Module 0
    while ((SYSCTL_RCGCGPIO_R & 0x02) == 0) {} // Wait for clock
    while ((SYSCTL_RCGCPWM_R & 0x01) == 0) {} // Wait for clock
    // Configure PB6 as PWM output (alternate function 4 = M0PWM0)
    GPIO_PORTB_AFSEL_R |= 0x40; // Enable alt function on PB6
    GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R & 0xF0FFFFFF) | 0x04000000; // AF4
    GPIO_PORTB_DEN_R |= 0x40; // Enable digital I/O on PB6
    GPIO_PORTB_AMSEL_R &= ~0x40; // Disable analog on PB6
    // PWM generator 0: count down, 440 Hz, 50% duty
    PWM0_0_CTL_R = 0; // Disable during setup
    PWM0_0_GENA_R = 0x8C; // High at LOAD, low at CMPA
    PWM0_0_LOAD_R = (SYSCLK / TONE_HZ) - 1; // 36362 -> 440 Hz
    PWM0_0_CMPA_R = PWM0_0_LOAD_R / 2; // 50% duty cycle
    PWM0_0_CTL_R = 1; // Enable generator
    PWM0_ENABLE_R |= 0x01; // Enable PWM output on PB6
    // start of PortB init
    SYSCTL_RCGCGPIO_R |= 0x22; // Enable clock for Port B   CHANGED 0x02 -> 0x22
    while ((SYSCTL_RCGCGPIO_R & 0x22) == 0) {} // Wait for clock
    GPIO_PORTB_AFSEL_R &= ~0x0F; // PB0-PB3 = plain GPIO
    GPIO_PORTB_DIR_R |= 0x0F;  //PB0-PB3 = output
    GPIO_PORTB_DEN_R |= 0x0F; // PB0-PB3 = digital
}

void sysdelay(uint32_t reload) {
    NVIC_ST_CTRL_R = 0; // Disable SysTick during setup
    NVIC_ST_RELOAD_R = reload - 1; // Set reload value
    NVIC_ST_CURRENT_R = 0; // Clear current value and COUNTFLAG
    NVIC_ST_CTRL_R = 0x05; // Enable + core clock, no interrupt
    while ((NVIC_ST_CTRL_R & COUNTFLAG) == 0) {} // Wait until count reaches 0
}

// handles button press
void GPIOF_Handler(void) {
    GPIO_PORTF_ICR_R = SW1; // Clear interrupt flag
    if ((GPIO_PORTF_DATA_R & SW1) == 0) {
        state = EVT_SW1_PRESS;
    }
}


void note(float keynum, uint32_t dur){
    float send_tone;
    // note = 0 is an easy way to have a pause
    if(keynum == 0) PWM0_0_CMPA_R = 0;
    else{
        send_tone = (pow(2,((keynum - 49) /12))) * 440;
        PWM0_0_LOAD_R = (SYSCLK / send_tone) - 1; 
        PWM0_0_CMPA_R = PWM0_0_LOAD_R * 0.5; // 50% duty
    }
    sysdelay(dur);
}

// SYSCLK/4 = quarter note
// SYSCLK/2 = half note
void twinkle(){
  uint32_t phrase = 0;
  while(1){
    if(phrase > 5) phrase = 0;
    switch (state) {

    // playing state
    // each note as a state would be very lengthy, but you can treat this as 6 phrase states
    // where each phrase goes either to the next phrase or to pause
    case EVT_PHRASE_READY:
        if(phrase == 0 || phrase == 4){
            GPIO_PORTB_DATA_R &= ~0x0F;
            GPIO_PORTB_DATA_R |= 0x05;
            note(40, SYSCLK/4);
            note(40, SYSCLK/4);
            note(47, SYSCLK/4);
            note(47, SYSCLK/4);
            GPIO_PORTB_DATA_R &= ~0x0F;
            GPIO_PORTB_DATA_R |= 0x0A;
            note(49, SYSCLK/4);
            note(49, SYSCLK/4);
            note(47, SYSCLK/2);
        }
        if(phrase == 1 || phrase == 5){
            GPIO_PORTB_DATA_R &= ~0x0F;
            GPIO_PORTB_DATA_R |= 0x0A;
            note(45, SYSCLK/4);
            note(45, SYSCLK/4);
            note(44, SYSCLK/4);
            note(44, SYSCLK/4);
            GPIO_PORTB_DATA_R &= ~0x0F;
            GPIO_PORTB_DATA_R |= 0x05;
            note(42, SYSCLK/4);
            note(42, SYSCLK/4);
            note(40, SYSCLK/2);
        }
        if(phrase == 2 || phrase == 3){
            GPIO_PORTB_DATA_R &= ~0x0F;
            GPIO_PORTB_DATA_R |= 0x00;
            note(47, SYSCLK/4);
            note(47, SYSCLK/4);
            note(45, SYSCLK/4);
            note(45, SYSCLK/4);
            GPIO_PORTB_DATA_R &= ~0x0F;
            GPIO_PORTB_DATA_R |= 0x0F;
            note(44, SYSCLK/4);
            note(44, SYSCLK/4);
            note(42, SYSCLK/2);
        }
    phrase++;
    break;

    // pause until switch pressed a second time
    case EVT_SW1_PRESS:
      state = EVT_NONE;
      while(1){
        note(0,SYSCLK/2);

        sysdelay(SYSCLK/2);
        if(state == EVT_SW1_PRESS){
          state = EVT_PHRASE_READY;
          break;
        } 
      }
    }
  }
}


int main(void) {
  Board_Init();

  note(0, SYSCLK/8); // turn off audio while waiting

// waits for button press to begin, then loops twinkle
  while(1){
    sysdelay(SYSCLK/2);
    if(state == EVT_SW1_PRESS){
      state = EVT_PHRASE_READY;
      while(1){
        twinkle();
        note(0,SYSCLK/2);
      }
    }
  }
}
