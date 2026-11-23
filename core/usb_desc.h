/*
 * usb_desc.h - USB device, configuration, HID and string descriptors for a
 * full-speed HID gamepad (one interface, one interrupt IN endpoint).
 */
#ifndef USB_DESC_H
#define USB_DESC_H

#include <stdint.h>

#define USB_VID 0x1209U /* pid.codes */
#define USB_PID 0x0001U /* pid.codes test PID: replace before distribution */
#define USB_BCD_DEVICE 0x0100U
#define USB_EP0_SIZE 64U
#define USB_EP1_IN_ADDR 0x81U
#define USB_EP1_SIZE 8U
#define USB_EP1_INTERVAL_MS 1U
#define USB_HID_IF 0U
#define USB_CONFIG_VALUE 1U
#define USB_UID_BYTES 12U

#define USB_DT_DEVICE 1U
#define USB_DT_CONFIG 2U
#define USB_DT_STRING 3U
#define USB_DT_INTERFACE 4U
#define USB_DT_ENDPOINT 5U
#define USB_DT_QUALIFIER 6U
#define USB_DT_HID 0x21U
#define USB_DT_HID_REPORT 0x22U

#define USB_STR_LANG 0U
#define USB_STR_MANUFACTURER 1U
#define USB_STR_PRODUCT 2U
#define USB_STR_SERIAL 3U
#define USB_STR_MAX_BYTES 64U

typedef struct
{
    const uint8_t *data;
    uint16_t len;
} usb_buf_t;

usb_buf_t usb_desc_device(void);
usb_buf_t usb_desc_config(void);
usb_buf_t usb_desc_hid(void); /* the 9-byte HID class descriptor inside the config */
usb_buf_t usb_desc_report(void);

/* Serial number string from the MCU unique ID (24 upper-case hex digits). */
void usb_desc_set_serial(const uint8_t uid[USB_UID_BYTES]);

/* Product string override (ASCII, truncated to fit); NULL restores the default. */
void usb_desc_set_product(const char *product);

/* Build string descriptor `index` (UTF-16LE). Empty buffer if unknown. */
usb_buf_t usb_desc_string(uint8_t index);

#endif /* USB_DESC_H */
