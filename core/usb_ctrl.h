/*
 * usb_ctrl.h - hardware-independent USB device state and EP0 request
 * handling for the HID gamepad. The driver (STM32 OTG FS, or the macOS
 * simulator) supplies usb_hw_ops_t and feeds SETUP packets in.
 *
 * The state machine is modelled in specs/UsbHid.tla.
 */
#ifndef USB_CTRL_H
#define USB_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#include "gp_report.h"

#define USB_SETUP_SIZE 8U

typedef enum
{
    USB_STATE_DEFAULT = 0,
    USB_STATE_ADDRESS = 1,
    USB_STATE_CONFIGURED = 2
} usb_dev_state_t;

typedef struct
{
    /* Send an IN data stage (driver splits into EP0-size packets and adds a
     * zero-length packet when `zlp` is true). len 0 = status-stage ZLP. */
    void (*ep0_send)(const uint8_t *data, uint16_t len, bool zlp);
    void (*ep0_stall)(void);
    void (*set_address)(uint8_t addr);
    /* config 0 = deconfigure (close EP1), else open EP1 IN. */
    void (*configure)(uint8_t config);
    /* Current input report for HID GET_REPORT. */
    void (*get_report)(uint8_t out[GP_REPORT_SIZE]);
} usb_hw_ops_t;

typedef struct
{
    uint8_t bm_request_type;
    uint8_t b_request;
    uint16_t w_value;
    uint16_t w_index;
    uint16_t w_length;
} usb_setup_t;

void usb_ctrl_init(const usb_hw_ops_t *ops);

/* Bus reset: back to the default state, address 0, EP1 closed. */
void usb_ctrl_bus_reset(void);

/* Decode an 8-byte SETUP packet (little endian fields). */
void usb_setup_parse(const uint8_t raw[USB_SETUP_SIZE], usb_setup_t *setup);

/* Handle a SETUP packet. Returns false if the request was stalled. */
bool usb_ctrl_setup(const uint8_t raw[USB_SETUP_SIZE]);

usb_dev_state_t usb_ctrl_state(void);
uint8_t usb_ctrl_address(void);
uint8_t usb_ctrl_idle_rate(void); /* 4 ms units, 0 = indefinite */
uint8_t usb_ctrl_protocol(void);  /* 1 = report protocol */

#endif /* USB_CTRL_H */
