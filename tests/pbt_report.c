/* Properties of report packing, descriptor agreement and TX scheduling. */
#include "pbt.h"
#include "hid_parse.h"
#include "gp_report.h"
#include "usb_desc.h"

static void gen_state(pbt_t *p, gp_state_t *s)
{
    s->buttons = (uint16_t)pbt_draw(p, 0xFFFFu);
    s->hat = (uint8_t)pbt_draw(p, GP_HAT_NULL);
    for (unsigned i = 0; i < GP_NUM_AXES; i++) s->axis[i] = (int8_t)pbt_range(p, -127, 127);
    pbt_note(p, "state{btn=0x%04x hat=%u axes=%d,%d,%d,%d} ", s->buttons, s->hat,
             s->axis[0], s->axis[1], s->axis[2], s->axis[3]);
}

static bool prop_roundtrip(pbt_t *p)
{
    gp_state_t s, back; uint8_t r[GP_REPORT_SIZE];
    gen_state(p, &s);
    uint8_t seq = (uint8_t)pbt_draw(p, 255u);
    gp_report_build(&s, seq, r);
    gp_report_parse(r, &back);
    if (back.buttons != s.buttons || back.hat != s.hat) return pbt_fail(p, "buttons/hat differ");
    for (unsigned i = 0; i < GP_NUM_AXES; i++)
        if (back.axis[i] != s.axis[i]) return pbt_fail(p, "axis %u: %d != %d", i, back.axis[i], s.axis[i]);
    if (r[7] != seq) return pbt_fail(p, "seq");
    return true;
}

/* What a host decodes through the report descriptor equals the state. */
static bool prop_descriptor_agrees(pbt_t *p)
{
    static hid_layout_t L; static int parsed;
    if (!parsed) { usb_buf_t d = usb_desc_report(); hid_parse(d.data, d.len, &L); parsed = 1; }
    gp_state_t s; uint8_t r[GP_REPORT_SIZE];
    gen_state(p, &s);
    gp_report_build(&s, 0u, r);
    for (int i = 0; i < L.nfields; i++) {
        const hid_field_t *f = &L.fields[i];
        int32_t v = hid_extract(r, f), want;
        if (f->is_const) { want = 0; }
        else if (f->usage_page == 0x09) { want = (s.buttons >> (f->usage - 1)) & 1; }
        else if (f->usage == 0x39) { want = s.hat; }
        else if (f->usage == 0x30) { want = s.axis[0]; }
        else if (f->usage == 0x31) { want = s.axis[1]; }
        else if (f->usage == 0x32) { want = s.axis[2]; }
        else if (f->usage == 0x35) { want = s.axis[3]; }
        else { continue; }
        if (v != want) return pbt_fail(p, "field page %x usage %x decodes %d, want %d", f->usage_page, f->usage, v, want);
        if (!f->is_const && !(f->has_null && v == GP_HAT_NULL) && (v < f->logical_min || v > f->logical_max))
            return pbt_fail(p, "usage %x value %d outside logical range", f->usage, v);
    }
    return true;
}

/* Scheduler: no change is lost, sequence numbers count sends, an unchanged
 * state is repeated only after the repeat period (host idle rate capped at
 * GP_TX_KEEPALIVE_MS), and never stays silent longer than that while the
 * endpoint is free. */
static bool prop_scheduler(pbt_t *p)
{
    gp_tx_t tx; gp_state_t s = { 0u, GP_HAT_NULL, { 0, 0, 0, 0 } };
    uint8_t out[GP_REPORT_SIZE], last[GP_REPORT_SIZE] = { 0 };
    uint32_t sends = 0, steps = pbt_draw(p, 400u), since = 0;
    uint8_t idle = (uint8_t)pbt_draw(p, 20u);
    /* per-case regimes: input changes every ~1..256 ms, host busy 0..75% */
    uint32_t change_mod = 1u << pbt_draw(p, 8u);
    uint32_t busy_mod = 1u + pbt_draw(p, 3u);
    uint32_t period = (idle != 0u && idle * 4u < GP_TX_KEEPALIVE_MS) ? idle * 4u : GP_TX_KEEPALIVE_MS;
    bool have = false;
    gp_tx_init(&tx);
    pbt_note(p, "idle=%u change=1/%u busy=1/%u ", idle, change_mod, busy_mod);
    for (uint32_t t = 0; t < steps; t++) {
        if (pbt_draw(p, change_mod - 1u) == 0u) { gen_state(p, &s); }
        bool ready = pbt_draw(p, busy_mod) != 0u;
        since++;
        bool sent = gp_tx_step(&tx, &s, idle, ready, out);
        uint8_t now[GP_REPORT_SIZE]; gp_report_build(&s, 0u, now);
        if (sent && !ready) return pbt_fail(p, "sent while endpoint busy at step %u", t);
        if (sent) {
            if (out[7] != (uint8_t)sends) return pbt_fail(p, "seq %u, expected %u", out[7], sends);
            if (have && memcmp(out, last, GP_REPORT_PAYLOAD) == 0 && since < period)
                return pbt_fail(p, "repeat after %u ms, period %u, at step %u", since, period, t);
            memcpy(last, out, sizeof last); have = true; sends++; since = 0;
        }
        if (ready && (!have || memcmp(last, now, GP_REPORT_PAYLOAD) != 0))
            return pbt_fail(p, "state change not delivered at step %u", t);
        if (ready && since > period)
            return pbt_fail(p, "silent for %u ms with the endpoint free (period %u) at step %u", since, period, t);
    }
    return true;
}

int main(void)
{
    int f = 0;
    f |= pbt_check("report_roundtrip", prop_roundtrip);
    f |= pbt_check("descriptor_agrees_with_packer", prop_descriptor_agrees);
    f |= pbt_check("tx_scheduler", prop_scheduler);
    return f;
}
