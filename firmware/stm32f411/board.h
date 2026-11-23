/*
 * board.h - Black Pill pin map (all inputs active low with pull-ups).
 *   Buttons 1-16 : PB0 PB1 PB3 PB4 PB5 PB6 PB7 PB8 PB9 PB10 PB12 PB13 PB14 PB15 PA8 PA15
 *   D-pad        : PA9 up, PA10 down, PC14 left, PC15 right
 *   Sticks (ADC) : PA1 X, PA2 Y, PA3 Z, PA4 Rz
 *   LED          : PC13 (active low)
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "gp_input.h"
#include "usb_desc.h"

void board_init(void);
void board_read(gp_raw_t *raw);
void board_led(bool on);
void board_uid(uint8_t uid[USB_UID_BYTES]);

#endif /* BOARD_H */
