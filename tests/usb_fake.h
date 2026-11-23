/* usb_fake.h - recording usb_hw_ops_t for host tests. */
#ifndef USB_FAKE_H
#define USB_FAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_ctrl.h"

typedef struct {
    uint8_t data[512];
    uint16_t len;
    bool zlp;
    bool sent;      /* ep0_send called since last reset */
    int stalls;
    int sends;
    int address;
    int config;
    uint8_t report[GP_REPORT_SIZE];
} usb_fake_t;

extern usb_fake_t g_fake;
extern const usb_hw_ops_t g_fake_ops;

void usb_fake_reset(void);
/* Issue a SETUP built from fields; returns usb_ctrl_setup's result. */
bool usb_fake_setup(uint8_t rt, uint8_t req, uint16_t value, uint16_t index, uint16_t length);

#endif
