/*
 * usb_ctrl.c - chapter 9 standard requests and HID class requests.
 */
#include "usb_ctrl.h"

#include <stddef.h>

#include "usb_desc.h"

#define REQ_GET_STATUS 0U
#define REQ_CLEAR_FEATURE 1U
#define REQ_SET_FEATURE 3U
#define REQ_SET_ADDRESS 5U
#define REQ_GET_DESCRIPTOR 6U
#define REQ_GET_CONFIGURATION 8U
#define REQ_SET_CONFIGURATION 9U
#define REQ_GET_INTERFACE 10U
#define REQ_SET_INTERFACE 11U

#define HID_GET_REPORT 0x01U
#define HID_GET_IDLE 0x02U
#define HID_GET_PROTOCOL 0x03U
#define HID_SET_REPORT 0x09U
#define HID_SET_IDLE 0x0AU
#define HID_SET_PROTOCOL 0x0BU

#define RT_DIR_IN 0x80U
#define RT_TYPE_MASK 0x60U
#define RT_TYPE_STANDARD 0x00U
#define RT_TYPE_CLASS 0x20U
#define RT_RECIP_MASK 0x1FU
#define RT_RECIP_DEVICE 0U
#define RT_RECIP_INTERFACE 1U
#define RT_RECIP_ENDPOINT 2U

typedef struct
{
    const usb_hw_ops_t *ops;
    usb_dev_state_t state;
    uint8_t address;
    uint8_t config;
    uint8_t idle_rate;
    uint8_t protocol;
    uint8_t scratch[GP_REPORT_SIZE];
} usb_ctrl_ctx_t;

static usb_ctrl_ctx_t s_ctx;

void usb_ctrl_init(const usb_hw_ops_t *ops)
{
    s_ctx.ops = ops;
    usb_ctrl_bus_reset();
}

void usb_ctrl_bus_reset(void)
{
    s_ctx.state = USB_STATE_DEFAULT;
    s_ctx.address = 0U;
    s_ctx.config = 0U;
    s_ctx.idle_rate = 0U;
    s_ctx.protocol = 1U;
}

void usb_setup_parse(const uint8_t raw[USB_SETUP_SIZE], usb_setup_t *setup)
{
    setup->bm_request_type = raw[0];
    setup->b_request = raw[1];
    setup->w_value = (uint16_t)((uint32_t)raw[2] | ((uint32_t)raw[3] << 8U));
    setup->w_index = (uint16_t)((uint32_t)raw[4] | ((uint32_t)raw[5] << 8U));
    setup->w_length = (uint16_t)((uint32_t)raw[6] | ((uint32_t)raw[7] << 8U));
}

usb_dev_state_t usb_ctrl_state(void)
{
    return s_ctx.state;
}

uint8_t usb_ctrl_address(void)
{
    return s_ctx.address;
}

uint8_t usb_ctrl_idle_rate(void)
{
    return s_ctx.idle_rate;
}

uint8_t usb_ctrl_protocol(void)
{
    return s_ctx.protocol;
}

/* Send `buf` truncated to wLength; a ZLP ends a short transfer that is a
 * multiple of the EP0 packet size. */
static bool reply(usb_buf_t buf, uint16_t w_length)
{
    uint16_t len = buf.len;
    bool zlp;

    if ((buf.data == NULL) || (len == 0U))
    {
        s_ctx.ops->ep0_stall();
        return false;
    }
    if (len > w_length)
    {
        len = w_length;
    }
    zlp = (len < w_length) && ((len % USB_EP0_SIZE) == 0U);
    s_ctx.ops->ep0_send(buf.data, len, zlp);
    return true;
}

static bool status_ok(void)
{
    s_ctx.ops->ep0_send(NULL, 0U, false);
    return true;
}

static bool stall(void)
{
    s_ctx.ops->ep0_stall();
    return false;
}

static bool get_descriptor(const usb_setup_t *s)
{
    uint8_t type = (uint8_t)(s->w_value >> 8U);
    uint8_t index = (uint8_t)(s->w_value & 0xFFU);
    uint8_t recip = s->bm_request_type & RT_RECIP_MASK;
    usb_buf_t buf = { NULL, 0U };

    if (recip == RT_RECIP_DEVICE)
    {
        switch (type)
        {
            case USB_DT_DEVICE:
                buf = usb_desc_device();
                break;
            case USB_DT_CONFIG:
                buf = usb_desc_config();
                break;
            case USB_DT_STRING:
                buf = usb_desc_string(index);
                break;
            default:
                /* device qualifier etc.: full-speed only, so stall */
                break;
        }
    }
    else if ((recip == RT_RECIP_INTERFACE) && (s->w_index == USB_HID_IF))
    {
        if (type == USB_DT_HID_REPORT)
        {
            buf = usb_desc_report();
        }
        else if (type == USB_DT_HID)
        {
            buf = usb_desc_hid();
        }
        else
        {
            buf.len = 0U;
        }
    }
    else
    {
        buf.len = 0U;
    }
    return reply(buf, s->w_length);
}

static bool set_address(const usb_setup_t *s)
{
    uint8_t addr = (uint8_t)(s->w_value & 0x7FU);

    if ((s->w_value > 127U) || (s->w_index != 0U) || (s->w_length != 0U) ||
        (s_ctx.state == USB_STATE_CONFIGURED))
    {
        return stall();
    }
    s_ctx.address = addr;
    s_ctx.state = (addr == 0U) ? USB_STATE_DEFAULT : USB_STATE_ADDRESS;
    s_ctx.ops->set_address(addr);
    return status_ok();
}

static bool set_configuration(const usb_setup_t *s)
{
    uint8_t cfg = (uint8_t)(s->w_value & 0xFFU);

    if ((s_ctx.state == USB_STATE_DEFAULT) || ((cfg != 0U) && (cfg != USB_CONFIG_VALUE)))
    {
        return stall();
    }
    s_ctx.config = cfg;
    s_ctx.state = (cfg == 0U) ? USB_STATE_ADDRESS : USB_STATE_CONFIGURED;
    s_ctx.ops->configure(cfg);
    return status_ok();
}

static bool small_reply(uint8_t b0, uint8_t b1, uint16_t len, uint16_t w_length)
{
    s_ctx.scratch[0] = b0;
    s_ctx.scratch[1] = b1;
    usb_buf_t buf = { s_ctx.scratch, len };
    return reply(buf, w_length);
}

static bool standard_request(const usb_setup_t *s)
{
    bool ok;

    switch (s->b_request)
    {
        case REQ_GET_DESCRIPTOR:
            ok = get_descriptor(s);
            break;
        case REQ_SET_ADDRESS:
            ok = set_address(s);
            break;
        case REQ_SET_CONFIGURATION:
            ok = set_configuration(s);
            break;
        case REQ_GET_CONFIGURATION:
            ok = small_reply(s_ctx.config, 0U, 1U, s->w_length);
            break;
        case REQ_GET_STATUS:
            ok = small_reply(0U, 0U, 2U, s->w_length);
            break;
        case REQ_GET_INTERFACE:
            ok = (s_ctx.state == USB_STATE_CONFIGURED) ? small_reply(0U, 0U, 1U, s->w_length) : stall();
            break;
        case REQ_SET_INTERFACE:
            ok = ((s_ctx.state == USB_STATE_CONFIGURED) && (s->w_value == 0U)) ? status_ok() : stall();
            break;
        case REQ_CLEAR_FEATURE:
        case REQ_SET_FEATURE:
            ok = status_ok();
            break;
        default:
            ok = stall();
            break;
    }
    return ok;
}

static bool hid_request(const usb_setup_t *s)
{
    bool ok;

    switch (s->b_request)
    {
        case HID_GET_REPORT:
            s_ctx.ops->get_report(s_ctx.scratch);
            {
                usb_buf_t buf = { s_ctx.scratch, (uint16_t)GP_REPORT_SIZE };
                ok = reply(buf, s->w_length);
            }
            break;
        case HID_GET_IDLE:
            ok = small_reply(s_ctx.idle_rate, 0U, 1U, s->w_length);
            break;
        case HID_GET_PROTOCOL:
            ok = small_reply(s_ctx.protocol, 0U, 1U, s->w_length);
            break;
        case HID_SET_IDLE:
            s_ctx.idle_rate = (uint8_t)(s->w_value >> 8U);
            ok = status_ok();
            break;
        case HID_SET_PROTOCOL:
            s_ctx.protocol = (uint8_t)(s->w_value & 1U);
            ok = status_ok();
            break;
        case HID_SET_REPORT:
        default:
            ok = stall();
            break;
    }
    return ok;
}

bool usb_ctrl_setup(const uint8_t raw[USB_SETUP_SIZE])
{
    usb_setup_t s;
    uint8_t type;
    uint8_t recip;
    bool ok;

    usb_setup_parse(raw, &s);
    type = s.bm_request_type & RT_TYPE_MASK;
    recip = s.bm_request_type & RT_RECIP_MASK;
    if (type == RT_TYPE_STANDARD)
    {
        ok = standard_request(&s);
    }
    else if ((type == RT_TYPE_CLASS) && (recip == RT_RECIP_INTERFACE) && (s.w_index == USB_HID_IF))
    {
        ok = hid_request(&s);
    }
    else
    {
        ok = stall();
    }
    return ok;
}
