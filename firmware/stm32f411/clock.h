/* clock.h - system clock and 1 kHz tick. */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define SYSCLK_HZ 96000000UL

/* HSE 25 MHz -> PLL: SYSCLK 96 MHz, APB1 48 MHz, APB2 96 MHz, USB 48 MHz.
 * Returns false (still on the 16 MHz HSI) if the crystal or PLL fail. */
bool clock_init(void);

/* Start SysTick at 1 kHz. */
void tick_init(void);

/* Milliseconds elapsed since the last call (0 if none). */
uint32_t tick_take(void);

/* Bounded busy wait, roughly `loops` iterations. */
void spin(uint32_t loops);

#endif /* CLOCK_H */
