/*
 * usb_desc.c - descriptor tables.
 *
 * The report descriptor declares a Generic Desktop / Game Pad application
 * collection, which macOS binds to its built-in HID driver (IOHIDFamily),
 * so no driver or kext is needed.
 */
#include "usb_desc.h"

#include <stddef.h>

static const uint8_t s_report[] = {
    0x05U, 0x01U,        /* Usage Page (Generic Desktop) */
    0x09U, 0x05U,        /* Usage (Game Pad) */
    0xA1U, 0x01U,        /* Collection (Application) */
    0x05U, 0x09U,        /*   Usage Page (Button) */
    0x19U, 0x01U,        /*   Usage Minimum (1) */
    0x29U, 0x10U,        /*   Usage Maximum (16) */
    0x15U, 0x00U,        /*   Logical Minimum (0) */
    0x25U, 0x01U,        /*   Logical Maximum (1) */
    0x75U, 0x01U,        /*   Report Size (1) */
    0x95U, 0x10U,        /*   Report Count (16) */
    0x81U, 0x02U,        /*   Input (Data, Var, Abs) */
    0x05U, 0x01U,        /*   Usage Page (Generic Desktop) */
    0x09U, 0x39U,        /*   Usage (Hat Switch) */
    0x15U, 0x00U,        /*   Logical Minimum (0) */
    0x25U, 0x07U,        /*   Logical Maximum (7) */
    0x35U, 0x00U,        /*   Physical Minimum (0) */
    0x46U, 0x3BU, 0x01U, /*   Physical Maximum (315) */
    0x65U, 0x14U,        /*   Unit (Degrees, English Rotation) */
    0x75U, 0x04U,        /*   Report Size (4) */
    0x95U, 0x01U,        /*   Report Count (1) */
    0x81U, 0x42U,        /*   Input (Data, Var, Abs, Null State) */
    0x65U, 0x00U,        /*   Unit (None) */
    0x45U, 0x00U,        /*   Physical Maximum (0) */
    0x75U, 0x04U,        /*   Report Size (4) */
    0x95U, 0x01U,        /*   Report Count (1) */
    0x81U, 0x03U,        /*   Input (Const, Var, Abs): padding */
    0x09U, 0x30U,        /*   Usage (X) */
    0x09U, 0x31U,        /*   Usage (Y) */
    0x09U, 0x32U,        /*   Usage (Z) */
    0x09U, 0x35U,        /*   Usage (Rz) */
    0x15U, 0x81U,        /*   Logical Minimum (-127) */
    0x25U, 0x7FU,        /*   Logical Maximum (127) */
    0x75U, 0x08U,        /*   Report Size (8) */
    0x95U, 0x04U,        /*   Report Count (4) */
    0x81U, 0x02U,        /*   Input (Data, Var, Abs) */
    0x06U, 0x00U, 0xFFU, /*   Usage Page (Vendor Defined 0xFF00) */
    0x09U, 0x01U,        /*   Usage (Vendor 1): sequence counter */
    0x15U, 0x00U,        /*   Logical Minimum (0) */
    0x26U, 0xFFU, 0x00U, /*   Logical Maximum (255) */
    0x75U, 0x08U,        /*   Report Size (8) */
    0x95U, 0x01U,        /*   Report Count (1) */
    0x81U, 0x02U,        /*   Input (Data, Var, Abs) */
    0xC0U,               /* End Collection */
};

#define REPORT_LEN ((uint16_t)sizeof(s_report))
#define CONFIG_LEN 34U

static const uint8_t s_device[18] = {
    18U, USB_DT_DEVICE,
    0x00U, 0x02U,             /* bcdUSB 2.00 */
    0x00U, 0x00U, 0x00U,      /* class defined at interface level */
    (uint8_t)USB_EP0_SIZE,
    (uint8_t)(USB_VID & 0xFFU), (uint8_t)(USB_VID >> 8U),
    (uint8_t)(USB_PID & 0xFFU), (uint8_t)(USB_PID >> 8U),
    (uint8_t)(USB_BCD_DEVICE & 0xFFU), (uint8_t)(USB_BCD_DEVICE >> 8U),
    USB_STR_MANUFACTURER, USB_STR_PRODUCT, USB_STR_SERIAL,
    1U,                       /* one configuration */
};

static const uint8_t s_config[CONFIG_LEN] = {
    /* Configuration */
    9U, USB_DT_CONFIG, (uint8_t)CONFIG_LEN, 0x00U, 1U, USB_CONFIG_VALUE, 0U,
    0x80U,                    /* bus powered */
    50U,                      /* 100 mA */
    /* Interface 0: HID, no boot subclass */
    9U, USB_DT_INTERFACE, USB_HID_IF, 0U, 1U, 0x03U, 0x00U, 0x00U, 0U,
    /* HID 1.11 */
    9U, USB_DT_HID, 0x11U, 0x01U, 0x00U, 1U, USB_DT_HID_REPORT,
    (uint8_t)(sizeof(s_report) & 0xFFU), (uint8_t)(sizeof(s_report) >> 8U),
    /* Endpoint 1 IN, interrupt */
    7U, USB_DT_ENDPOINT, USB_EP1_IN_ADDR, 0x03U, (uint8_t)USB_EP1_SIZE, 0x00U,
    (uint8_t)USB_EP1_INTERVAL_MS,
};

#define HID_DESC_OFFSET 18U
#define HID_DESC_LEN 9U

static const char s_manufacturer[] = "Nool";
static const char s_default_product[] = "Nool STM32 Gamepad";
static char s_product[32] = "Nool STM32 Gamepad";
static char s_serial[(USB_UID_BYTES * 2U) + 1U] = "000000000000000000000000";
static uint8_t s_string_buf[USB_STR_MAX_BYTES];

usb_buf_t usb_desc_device(void)
{
    usb_buf_t b = { s_device, (uint16_t)sizeof(s_device) };
    return b;
}

usb_buf_t usb_desc_config(void)
{
    usb_buf_t b = { s_config, (uint16_t)CONFIG_LEN };
    return b;
}

usb_buf_t usb_desc_hid(void)
{
    usb_buf_t b = { &s_config[HID_DESC_OFFSET], (uint16_t)HID_DESC_LEN };
    return b;
}

usb_buf_t usb_desc_report(void)
{
    usb_buf_t b = { s_report, REPORT_LEN };
    return b;
}

void usb_desc_set_serial(const uint8_t uid[USB_UID_BYTES])
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t i;

    for (i = 0U; i < USB_UID_BYTES; i++)
    {
        s_serial[2U * i] = hex[(uid[i] >> 4U) & 0x0FU];
        s_serial[(2U * i) + 1U] = hex[uid[i] & 0x0FU];
    }
    s_serial[USB_UID_BYTES * 2U] = '\0';
}

void usb_desc_set_product(const char *product)
{
    const char *src = (product != NULL) ? product : s_default_product;
    uint32_t i;

    for (i = 0U; i < (sizeof(s_product) - 1U); i++)
    {
        s_product[i] = src[i];
        if (src[i] == '\0')
        {
            break;
        }
    }
    s_product[sizeof(s_product) - 1U] = '\0';
}

static usb_buf_t ascii_to_string(const char *text)
{
    uint32_t n = 0U;
    usb_buf_t b;

    while ((n < ((USB_STR_MAX_BYTES - 2U) / 2U)) && (text[n] != '\0'))
    {
        s_string_buf[2U + (2U * n)] = (uint8_t)text[n];
        s_string_buf[3U + (2U * n)] = 0U;
        n++;
    }
    s_string_buf[0] = (uint8_t)(2U + (2U * n));
    s_string_buf[1] = USB_DT_STRING;
    b.data = s_string_buf;
    b.len = (uint16_t)s_string_buf[0];
    return b;
}

usb_buf_t usb_desc_string(uint8_t index)
{
    usb_buf_t b = { s_string_buf, 0U };

    switch (index)
    {
        case USB_STR_LANG:
            s_string_buf[0] = 4U;
            s_string_buf[1] = USB_DT_STRING;
            s_string_buf[2] = 0x09U; /* en-US */
            s_string_buf[3] = 0x04U;
            b.len = 4U;
            break;
        case USB_STR_MANUFACTURER:
            b = ascii_to_string(s_manufacturer);
            break;
        case USB_STR_PRODUCT:
            b = ascii_to_string(s_product);
            break;
        case USB_STR_SERIAL:
            b = ascii_to_string(s_serial);
            break;
        default:
            b.len = 0U;
            break;
    }
    return b;
}
