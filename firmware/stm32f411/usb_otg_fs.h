/*
 * usb_otg_fs.h - STM32F4 OTG FS device driver for the HID gamepad.
 * Polled from the superloop (no USB interrupt), so control handling and
 * report transmission never race.
 */
#ifndef USB_OTG_FS_H
#define USB_OTG_FS_H

#include <stdbool.h>
#include <stdint.h>

#include "gp_report.h"

/* Bring up the core and connect to the bus. `get_report` serves GET_REPORT. */
bool usb_init(void (*get_report)(uint8_t out[GP_REPORT_SIZE]));

/* Service pending USB events (call as often as possible). */
void usb_poll(void);

/* True when configured and EP1 IN can take a report. */
bool usb_ep1_ready(void);

/* Queue one input report on EP1 IN (caller checks usb_ep1_ready()). */
void usb_ep1_send(const uint8_t report[GP_REPORT_SIZE]);

/* True once after every bus reset or (de)configuration: the application
 * must re-initialise its report scheduler (specs/UsbHid.tla, ReinitScheduler). */
bool usb_take_link_change(void);

#endif /* USB_OTG_FS_H */
