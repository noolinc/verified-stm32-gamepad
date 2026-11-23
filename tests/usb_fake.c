#include "usb_fake.h"

#include <string.h>

usb_fake_t g_fake;

static void f_send(const uint8_t *data, uint16_t len, bool zlp)
{
    g_fake.len = len;
    g_fake.zlp = zlp;
    g_fake.sent = true;
    g_fake.sends++;
    if (data != NULL && len > 0U) {
        memcpy(g_fake.data, data, len > sizeof g_fake.data ? sizeof g_fake.data : len);
    }
}
static void f_stall(void) { g_fake.stalls++; }
static void f_addr(uint8_t a) { g_fake.address = a; }
static void f_cfg(uint8_t c) { g_fake.config = c; }
static void f_report(uint8_t out[GP_REPORT_SIZE]) { memcpy(out, g_fake.report, GP_REPORT_SIZE); }

const usb_hw_ops_t g_fake_ops = { f_send, f_stall, f_addr, f_cfg, f_report };

void usb_fake_reset(void)
{
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.address = -1;
    g_fake.config = -1;
    usb_ctrl_init(&g_fake_ops);
}

bool usb_fake_setup(uint8_t rt, uint8_t req, uint16_t value, uint16_t index, uint16_t length)
{
    uint8_t raw[8] = { rt, req, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index,
                       (uint8_t)(index >> 8), (uint8_t)length, (uint8_t)(length >> 8) };
    g_fake.sent = false;
    g_fake.len = 0;
    return usb_ctrl_setup(raw);
}
