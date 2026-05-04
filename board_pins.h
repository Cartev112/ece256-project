#ifndef BOARD_PINS_H_
#define BOARD_PINS_H_

/*
 * GPIO Port F bit assignments on the EK-TM4C123GXL LaunchPad:
 *   PF1 = red LED
 *   PF2 = blue LED
 *   PF3 = green LED
 *   PF4 = SW1 pushbutton, active low
 */
#define LED_RED_PIN                 0x02U
#define LED_BLUE_PIN                0x04U
#define LED_GREEN_PIN               0x08U
#define LED_YELLOW_PINS             (LED_RED_PIN | LED_GREEN_PIN)
#define LED_CYAN_PINS               (LED_BLUE_PIN | LED_GREEN_PIN)
#define LED_MAGENTA_PINS            (LED_RED_PIN | LED_BLUE_PIN)
#define LED_WHITE_PINS              (LED_RED_PIN | LED_BLUE_PIN | LED_GREEN_PIN)
#define RGB_LED_PINS                LED_WHITE_PINS
#define SW1_PIN                     0x10U

/*
 * PB0-PB3 drive the external LED bank from the teammate prototype. PB6 is
 * M0PWM0 for audio output.
 */
#define PORTB_LED_PINS              0x0FU
#define AUDIO_PWM_PIN               0x40U
#define GPIO_PCTL_PB6_M0PWM0_VALUE  0x04000000U

/*
 * UART0 uses the standard LaunchPad serial pins:
 *   PA0 = U0RX
 *   PA1 = U0TX
 */
#define UART0_RXTX_PINS             0x03U
#define GPIO_PCTL_PA0_U0RX_VALUE    0x00000001U
#define GPIO_PCTL_PA1_U0TX_VALUE    0x00000010U

#endif
