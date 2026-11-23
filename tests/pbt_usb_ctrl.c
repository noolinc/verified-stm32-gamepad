/* Properties of the EP0 request handler under random SETUP traffic. */
#include "pbt.h"
#include "usb_fake.h"
#include "usb_desc.h"

static const uint8_t k_types[] = { 0x00, 0x80, 0x01, 0x81, 0x02, 0x82, 0x21, 0xA1, 0x40, 0xC0, 0x22 };

static bool one_request(pbt_t *p, uint32_t t)
{
    uint8_t rt = pbt_bool(p) ? k_types[pbt_draw(p, sizeof k_types - 1u)] : (uint8_t)pbt_draw(p, 255u);
    uint8_t req = pbt_bool(p) ? (uint8_t)pbt_draw(p, 12u) : (uint8_t)pbt_draw(p, 255u);
    uint16_t val = pbt_bool(p) ? (uint16_t)((pbt_draw(p, 0x22u) << 8) | pbt_draw(p, 4u)) : (uint16_t)pbt_draw(p, 0xFFFFu);
    uint16_t idx = pbt_bool(p) ? 0u : (uint16_t)pbt_draw(p, 0xFFFFu);
    uint16_t len = (uint16_t)pbt_draw(p, 0xFFFFu);
    int stalls = g_fake.stalls, sends = g_fake.sends;
    pbt_note(p, "[%02x %02x %04x %04x %u] ", rt, req, val, idx, len);
    bool ok = usb_fake_setup(rt, req, val, idx, len);
    int ds = g_fake.stalls - stalls, dn = g_fake.sends - sends;
    if (ds + dn != 1) return pbt_fail(p, "step %u: %d stalls + %d sends (want exactly one)", t, ds, dn);
    if (ok != (dn == 1)) return pbt_fail(p, "step %u: return %d but stalled=%d", t, ok, ds);
    if (dn == 1 && g_fake.len > len) return pbt_fail(p, "step %u: replied %u bytes > wLength %u", t, g_fake.len, len);
    if (dn == 1 && g_fake.zlp && !(g_fake.len < len && g_fake.len % USB_EP0_SIZE == 0u))
        return pbt_fail(p, "step %u: bad ZLP len=%u wLength=%u", t, g_fake.len, len);
    if ((rt & 0x60u) == 0x40u && ok) return pbt_fail(p, "vendor request accepted");
    return true;
}

static bool prop_requests(pbt_t *p)
{
    uint32_t n = pbt_draw(p, 40u);
    usb_fake_reset();
    for (uint32_t t = 0; t < n; t++) {
        if (pbt_draw(p, 15u) == 0u) { usb_ctrl_bus_reset(); pbt_note(p, "RESET "); }
        else if (!one_request(p, t)) { return false; }
        usb_dev_state_t st = usb_ctrl_state();
        if (st == USB_STATE_CONFIGURED && (usb_ctrl_address() == 0u || g_fake.config != 1))
            return pbt_fail(p, "configured with address %u config %d", usb_ctrl_address(), g_fake.config);
        if (st == USB_STATE_DEFAULT && usb_ctrl_address() != 0u)
            return pbt_fail(p, "default state with address %u", usb_ctrl_address());
    }
    return true;
}

int main(void)
{
    return pbt_check("usb_ctrl_random_setup", prop_requests);
}
