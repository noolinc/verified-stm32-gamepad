/* Properties of debounce, hat mapping and stick scaling. */
#include "pbt.h"
#include "gp_input.h"

static void gen_cal(pbt_t *p, gp_axis_cal_t *c)
{
    c->center = (uint16_t)pbt_range(p, 1024, 3071);
    c->deadzone = (uint16_t)pbt_draw(p, 512u);
    c->invert = pbt_bool(p);
    pbt_note(p, "cal{c=%u dz=%u inv=%d} ", c->center, c->deadzone, c->invert);
}

static bool prop_axis_bounded_monotonic(pbt_t *p)
{
    gp_axis_cal_t c; gen_cal(p, &c);
    uint16_t a = (uint16_t)pbt_draw(p, GP_ADC_MAX), b = (uint16_t)pbt_draw(p, GP_ADC_MAX);
    if (a > b) { uint16_t t = a; a = b; b = t; }
    pbt_note(p, "a=%u b=%u ", a, b);
    int va = gp_axis_scale(a, &c), vb = gp_axis_scale(b, &c);
    if (va < -127 || va > 127 || vb < -127 || vb > 127) return pbt_fail(p, "out of range %d %d", va, vb);
    if (c.invert ? (va < vb) : (va > vb)) return pbt_fail(p, "not monotonic: f(%u)=%d f(%u)=%d", a, va, b, vb);
    int da = (int)a - (int)c.center;
    if ((da < 0 ? -da : da) <= (int)c.deadzone && va != 0) return pbt_fail(p, "nonzero %d inside deadzone", va);
    if (gp_axis_scale(0u, &c) != (c.invert ? 127 : -127)) return pbt_fail(p, "full low deflection");
    if (gp_axis_scale(GP_ADC_MAX, &c) != (c.invert ? -127 : 127)) return pbt_fail(p, "full high deflection");
    return true;
}

static bool prop_axis_odd_symmetric(pbt_t *p)
{
    gp_axis_cal_t c = { 2048u, (uint16_t)pbt_draw(p, 512u), false };
    uint16_t d = (uint16_t)pbt_draw(p, 2047u);
    int up = gp_axis_scale((uint16_t)(2048u + d), &c), down = gp_axis_scale((uint16_t)(2048u - d), &c);
    pbt_note(p, "dz=%u d=%u ", c.deadzone, d);
    if (up + down > 1 || up + down < -1) return pbt_fail(p, "f(c+%u)=%d f(c-%u)=%d", d, up, d, down);
    return true;
}

static bool prop_hat(pbt_t *p)
{
    uint32_t in = pbt_draw(p, 15u) << 16;
    uint8_t h = gp_hat_from_dpad(in);
    int dy = ((in & GP_IN_DOWN) ? 1 : 0) - ((in & GP_IN_UP) ? 1 : 0);
    int dx = ((in & GP_IN_RIGHT) ? 1 : 0) - ((in & GP_IN_LEFT) ? 1 : 0);
    static const int hx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, hy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    pbt_note(p, "dpad=0x%x ", in >> 16);
    if (h > GP_HAT_NULL) return pbt_fail(p, "hat %u out of range", h);
    if (h == GP_HAT_NULL) return (dx == 0 && dy == 0) ? true : pbt_fail(p, "null hat for dx=%d dy=%d", dx, dy);
    if (hx[h] != dx || hy[h] != dy) return pbt_fail(p, "hat %u for dx=%d dy=%d", h, dx, dy);
    return true;
}

/* Debounce: output follows a stable input within the window, never moves
 * away from the current raw value, and fewer than N net presses from rest
 * never register. */
static bool prop_debounce(pbt_t *p)
{
    gp_input_t ctx; gp_raw_t raw = { 0u, { 2048u, 2048u, 2048u, 2048u } }; gp_state_t s;
    uint32_t steps = pbt_draw(p, 120u), run = 0, pressed_total = 0;
    int prev_raw = -1, prev_out = 0;
    gp_input_init(&ctx);
    for (uint32_t t = 0; t < steps; t++) {
        int bit = (int)pbt_draw(p, 1u);
        pbt_note(p, "%d", bit);
        run = (bit == prev_raw) ? run + 1u : 1u;
        prev_raw = bit;
        pressed_total += (uint32_t)bit;
        raw.inputs = (uint32_t)bit;
        gp_input_step(&ctx, &raw, &s);
        int out = (int)(s.buttons & 1u);
        if (out != prev_out && out != bit) return pbt_fail(p, "output moved to %d against raw %d at t=%u", out, bit, t);
        if (run >= GP_DEBOUNCE_MS && out != bit) return pbt_fail(p, "input stable %u ticks but output %d at t=%u", run, out, t);
        if (pressed_total < GP_DEBOUNCE_MS && out != 0) return pbt_fail(p, "press after only %u pressed ticks", pressed_total);
        prev_out = out;
    }
    return true;
}

int main(void)
{
    int f = 0;
    f |= pbt_check("axis_bounded_monotonic_deadzone", prop_axis_bounded_monotonic);
    f |= pbt_check("axis_odd_symmetric", prop_axis_odd_symmetric);
    f |= pbt_check("hat_from_dpad", prop_hat);
    f |= pbt_check("debounce", prop_debounce);
    return f;
}
