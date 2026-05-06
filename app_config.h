#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

/*
 * The TM4C LaunchPad comes out of reset using the internal 16 MHz system
 * clock. This project keeps that default while the focus is FSM, UART, and
 * basic PWM audio behavior.
 */
#define SYSTEM_CLOCK_HZ             16000000U
#define PWM_CLOCK_HZ                SYSTEM_CLOCK_HZ

/*
 * SysTick drives the foreground scheduler every 5 ms. Song durations and
 * button debounce are expressed in these ticks.
 */
#define SYSTICK_PERIOD_MS           5U
#define DEBOUNCE_SAMPLES            3U

/*
 * Short setup states keep the expanded FSM explicit without adding a long
 * audible delay between notes.
 */
#define LOAD_PHRASE_TICKS           1U
#define LOAD_NOTE_TICKS             1U
#define NOTE_GAP_TICKS              10U     /* 50 ms */

#endif
