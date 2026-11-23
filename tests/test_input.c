#include "check.h"
#include "gp_input.h"

static void test_debounce(void)
{
    gp_input_t ctx; gp_raw_t raw = { 0u, { 2048u, 2048u, 2048u, 2048u } }; gp_state_t s;
    gp_input_init(&ctx);
    raw.inputs = 1u;
    for (unsigned i = 0; i < GP_DEBOUNCE_MS - 1u; i++) { gp_input_step(&ctx, &raw, &s); CHECK_EQ(s.buttons, 0); }
    gp_input_step(&ctx, &raw, &s); CHECK_EQ(s.buttons, 1);
    raw.inputs = 0u;
    gp_input_step(&ctx, &raw, &s); CHECK_EQ(s.buttons, 1);   /* release also debounced */
    for (unsigned i = 0; i < GP_DEBOUNCE_MS; i++) gp_input_step(&ctx, &raw, &s);
    CHECK_EQ(s.buttons, 0);
}

static void test_hat(void)
{
    CHECK_EQ(gp_hat_from_dpad(0u), GP_HAT_NULL);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_UP), 0);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_UP | GP_IN_RIGHT), 1);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_RIGHT), 2);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_DOWN | GP_IN_RIGHT), 3);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_DOWN), 4);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_DOWN | GP_IN_LEFT), 5);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_LEFT), 6);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_UP | GP_IN_LEFT), 7);
    CHECK_EQ(gp_hat_from_dpad(GP_IN_UP | GP_IN_DOWN), GP_HAT_NULL);
}

static void test_axis(void)
{
    gp_axis_cal_t c = { 2048u, 96u, false };
    CHECK_EQ(gp_axis_scale(2048u, &c), 0);
    CHECK_EQ(gp_axis_scale(2048u + 96u, &c), 0);
    CHECK_EQ(gp_axis_scale(4095u, &c), 127);
    CHECK_EQ(gp_axis_scale(0u, &c), -127);
    c.invert = true;
    CHECK_EQ(gp_axis_scale(4095u, &c), -127);
}

int main(void)
{
    test_debounce(); test_hat(); test_axis();
    return check_done("test_input");
}
