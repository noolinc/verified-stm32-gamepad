/*
 * usb_otg_fs.c - register-level OTG FS device (RM0383 chapter 22).
 *
 * FIFO RAM (320 words): RX 128, EP0 TX 32, EP1 TX 32.
 */
#include "usb_otg_fs.h"

#include <stddef.h>

#include "clock.h"
#include "regs.h"
#include "usb_ctrl.h"
#include "usb_desc.h"

#define WAIT_LOOPS 200000UL
#define RX_FIFO_WORDS 128UL
#define EP0_TX_WORDS 32UL
#define EP1_TX_WORDS 32UL
#define PKTSTS_OUT_DATA 2UL
#define PKTSTS_SETUP_DATA 6UL
#define EP0_STUPCNT3 ((3UL << 29U) | (1UL << 19U) | 24UL)
#define EP1_TYPE_INTERRUPT (3UL << 18U)
#define EP1_TXFNUM (1UL << 22U)
#define EP1_MCNT1 (1UL << 29U)

typedef struct
{
    const uint8_t *data;
    uint16_t left;
    bool zlp;
    bool active;
} ep0_tx_t;

static ep0_tx_t s_ep0;
static uint8_t s_setup[USB_SETUP_SIZE];
static bool s_configured;
static bool s_link_change;
static void (*s_get_report)(uint8_t out[GP_REPORT_SIZE]);

static bool wait_clear(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t n;

    for (n = 0U; n < WAIT_LOOPS; n++)
    {
        if ((*reg & mask) == 0U)
        {
            return true;
        }
    }
    return false;
}

static void flush_tx(uint32_t fifo)
{
    OTG_GRSTCTL = GRST_TXFFLSH | (fifo << 6U);
    (void)wait_clear(&OTG_GRSTCTL, GRST_TXFFLSH);
}

static void ep0_arm_setup(void)
{
    OTG_DOEPTSIZ(0U) = EP0_STUPCNT3;
    OTG_DOEPCTL(0U) |= EP_ENA | EP_CNAK;
}

static void write_fifo(uint32_t ep, const uint8_t *data, uint16_t len)
{
    uint32_t words = ((uint32_t)len + 3U) / 4U;
    uint32_t w;

    for (w = 0U; w < words; w++)
    {
        uint32_t v = 0U;
        uint32_t b;
        for (b = 0U; b < 4U; b++)
        {
            uint32_t idx = (4U * w) + b;
            if (idx < len)
            {
                v |= (uint32_t)data[idx] << (8U * b);
            }
        }
        OTG_FIFO(ep) = v;
    }
}

/* Send the next EP0 IN packet of the pending transfer. */
static void ep0_next_packet(void)
{
    uint16_t n = (s_ep0.left > USB_EP0_SIZE) ? (uint16_t)USB_EP0_SIZE : s_ep0.left;

    OTG_DIEPTSIZ(0U) = (1UL << 19U) | n;
    OTG_DIEPCTL(0U) |= EP_ENA | EP_CNAK;
    if (n > 0U)
    {
        write_fifo(0U, s_ep0.data, n);
    }
    s_ep0.data = (s_ep0.data != NULL) ? &s_ep0.data[n] : NULL;
    s_ep0.left = (uint16_t)(s_ep0.left - n);
    if ((s_ep0.left == 0U) && (n == USB_EP0_SIZE) && s_ep0.zlp)
    {
        s_ep0.zlp = false; /* one more zero-length packet follows */
    }
    else if (s_ep0.left == 0U)
    {
        s_ep0.active = false;
    }
    else
    {
        /* more data packets follow */
    }
}

/* ---- usb_hw_ops_t for core/usb_ctrl.c ---- */
static void hw_ep0_send(const uint8_t *data, uint16_t len, bool zlp)
{
    s_ep0.data = data;
    s_ep0.left = len;
    s_ep0.zlp = zlp;
    s_ep0.active = true;
    ep0_next_packet();
    if (len > 0U)
    {
        ep0_arm_setup(); /* accept the OUT status stage (and any new SETUP) */
    }
}

static void hw_ep0_stall(void)
{
    OTG_DIEPCTL(0U) |= EP_STALL;
    OTG_DOEPCTL(0U) |= EP_STALL;
    ep0_arm_setup();
}

static void hw_set_address(uint8_t addr)
{
    OTG_DCFG = (OTG_DCFG & ~(0x7FUL << 4U)) | ((uint32_t)addr << 4U);
}

static void ep1_close(void)
{
    if ((OTG_DIEPCTL(1U) & EP_ENA) != 0U)
    {
        OTG_DIEPCTL(1U) |= EP_DIS | EP_SNAK;
        (void)wait_clear(&OTG_DIEPCTL(1U), EP_ENA);
    }
    OTG_DIEPCTL(1U) &= ~EP_USBAEP;
    flush_tx(1U);
}

static void hw_configure(uint8_t config)
{
    ep1_close();
    s_configured = (config != 0U);
    if (s_configured)
    {
        OTG_DIEPCTL(1U) = EP_USBAEP | EP1_TYPE_INTERRUPT | EP1_TXFNUM | EP_SD0PID | EP_SNAK |
                          (uint32_t)USB_EP1_SIZE;
        OTG_DAINTMSK |= (1UL << 1U);
    }
    s_link_change = true;
}

static void hw_get_report(uint8_t out[GP_REPORT_SIZE])
{
    s_get_report(out);
}

static const usb_hw_ops_t s_ops = {
    hw_ep0_send, hw_ep0_stall, hw_set_address, hw_configure, hw_get_report,
};

/* ---- core bring-up ---- */
static bool wait_set(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t n;

    for (n = 0U; n < WAIT_LOOPS; n++)
    {
        if ((*reg & mask) != 0U)
        {
            return true;
        }
    }
    return false;
}

static bool core_reset(void)
{
    if (!wait_set(&OTG_GRSTCTL, GRST_AHBIDL))
    {
        return false;
    }
    OTG_GRSTCTL = GRST_CSRST;
    if (!wait_clear(&OTG_GRSTCTL, GRST_CSRST))
    {
        return false;
    }
    return wait_set(&OTG_GRSTCTL, GRST_AHBIDL);
}

static void setup_fifos(void)
{
    OTG_GRXFSIZ = RX_FIFO_WORDS;
    OTG_DIEPTXF0 = (EP0_TX_WORDS << 16U) | RX_FIFO_WORDS;
    OTG_DIEPTXF1 = (EP1_TX_WORDS << 16U) | (RX_FIFO_WORDS + EP0_TX_WORDS);
    flush_tx(0x10U);
    OTG_GRSTCTL = GRST_RXFFLSH;
    (void)wait_clear(&OTG_GRSTCTL, GRST_RXFFLSH);
}

static void usb_pins(void)
{
    RCC_AHB1ENR |= 1UL;                                       /* GPIOA */
    GPIO_MODER(GPIOA_BASE) = (GPIO_MODER(GPIOA_BASE) & ~(0xFUL << 22U)) | (0xAUL << 22U);
    GPIO_OSPEEDR(GPIOA_BASE) |= (0xFUL << 22U);                /* very high */
    GPIO_AFRH(GPIOA_BASE) = (GPIO_AFRH(GPIOA_BASE) & ~(0xFFUL << 12U)) | (0xAAUL << 12U); /* AF10 */
    RCC_AHB2ENR |= (1UL << 7U);                               /* OTGFSEN */
}

bool usb_init(void (*get_report)(uint8_t out[GP_REPORT_SIZE]))
{
    s_get_report = get_report;
    usb_ctrl_init(&s_ops);
    usb_pins();
    OTG_GUSBCFG = (1UL << 30U) | (1UL << 6U) | (6UL << 10U); /* force device, FS PHY, TRDT 6 */
    if (!core_reset())
    {
        return false;
    }
    spin(500000UL); /* force-mode settle time (>25 ms) */
    OTG_GCCFG = (1UL << 16U) | (1UL << 21U); /* PWRDWN off, no VBUS sensing */
    OTG_PCGCCTL = 0U;
    OTG_DCFG = 3UL; /* full speed */
    setup_fifos();
    OTG_DIEPMSK = EPINT_XFRC;
    OTG_DOEPMSK = EPINT_XFRC | EPINT_STUP;
    OTG_GINTSTS = 0xFFFFFFFFUL;
    OTG_GINTMSK = GINT_USBRST | GINT_ENUMDNE | GINT_RXFLVL | GINT_IEPINT | GINT_OEPINT;
    OTG_DCTL &= ~(1UL << 1U); /* soft connect */
    return true;
}

/* ---- event handling ---- */
static void on_bus_reset(void)
{
    uint32_t ep;

    flush_tx(0x10U);
    for (ep = 0U; ep < 4U; ep++)
    {
        OTG_DIEPINT(ep) = 0xFFUL;
        OTG_DOEPINT(ep) = 0xFFUL;
        OTG_DOEPCTL(ep) |= EP_SNAK;
    }
    OTG_DIEPCTL(1U) = 0U;
    OTG_DAINTMSK = (1UL << 0U) | (1UL << 16U);
    OTG_DCFG &= ~(0x7FUL << 4U);
    s_ep0.active = false;
    s_configured = false;
    s_link_change = true;
    usb_ctrl_bus_reset();
    ep0_arm_setup();
}

static void on_rx_level(void)
{
    uint32_t st = OTG_GRXSTSP;
    uint32_t bcnt = (st >> 4U) & 0x7FFUL;
    uint32_t pktsts = (st >> 17U) & 0xFUL;
    uint32_t w;

    if ((pktsts == PKTSTS_SETUP_DATA) && (bcnt == USB_SETUP_SIZE))
    {
        for (w = 0U; w < 2U; w++)
        {
            uint32_t v = OTG_FIFO(0U);
            s_setup[4U * w] = (uint8_t)v;
            s_setup[(4U * w) + 1U] = (uint8_t)(v >> 8U);
            s_setup[(4U * w) + 2U] = (uint8_t)(v >> 16U);
            s_setup[(4U * w) + 3U] = (uint8_t)(v >> 24U);
        }
    }
    else if (pktsts == PKTSTS_OUT_DATA)
    {
        for (w = 0U; w < ((bcnt + 3U) / 4U); w++)
        {
            (void)OTG_FIFO(0U); /* EP0 OUT data/status: discard */
        }
    }
    else
    {
        /* transfer/setup-complete markers carry no data */
    }
}

static void on_out_ep0(void)
{
    uint32_t flags = OTG_DOEPINT(0U);

    OTG_DOEPINT(0U) = flags;
    if ((flags & EPINT_STUP) != 0U)
    {
        s_ep0.active = false;
        (void)usb_ctrl_setup(s_setup);
    }
    if ((flags & EPINT_XFRC) != 0U)
    {
        ep0_arm_setup();
    }
}

static void on_in_endpoints(void)
{
    uint32_t daint = OTG_DAINT;

    if ((daint & 1UL) != 0U)
    {
        uint32_t flags = OTG_DIEPINT(0U);
        OTG_DIEPINT(0U) = flags;
        if (((flags & EPINT_XFRC) != 0U) && s_ep0.active)
        {
            ep0_next_packet();
        }
    }
    if ((daint & 2UL) != 0U)
    {
        OTG_DIEPINT(1U) = OTG_DIEPINT(1U);
    }
}

void usb_poll(void)
{
    uint32_t sts = OTG_GINTSTS & OTG_GINTMSK;

    if ((sts & GINT_USBRST) != 0U)
    {
        OTG_GINTSTS = GINT_USBRST;
        on_bus_reset();
    }
    if ((sts & GINT_ENUMDNE) != 0U)
    {
        OTG_GINTSTS = GINT_ENUMDNE;
        OTG_DIEPCTL(0U) &= ~3UL; /* MPSIZ = 64 bytes */
        OTG_DCTL |= (1UL << 8U); /* CGINAK */
    }
    if ((sts & GINT_RXFLVL) != 0U)
    {
        on_rx_level();
    }
    if ((sts & GINT_OEPINT) != 0U)
    {
        on_out_ep0();
    }
    if ((sts & GINT_IEPINT) != 0U)
    {
        on_in_endpoints();
    }
}

bool usb_ep1_ready(void)
{
    return s_configured && ((OTG_DIEPCTL(1U) & EP_ENA) == 0U);
}

void usb_ep1_send(const uint8_t report[GP_REPORT_SIZE])
{
    OTG_DIEPTSIZ(1U) = EP1_MCNT1 | (1UL << 19U) | GP_REPORT_SIZE;
    OTG_DIEPCTL(1U) |= EP_ENA | EP_CNAK;
    write_fifo(1U, report, (uint16_t)GP_REPORT_SIZE);
}

bool usb_take_link_change(void)
{
    bool changed = s_link_change;

    s_link_change = false;
    return changed;
}
