#include "check.h"
#include "gp_report.h"

static void test_layout(void)
{
    gp_state_t s = { 0x8001u, 3u, { -127, 127, 0, -5 } };
    uint8_t r[GP_REPORT_SIZE];
    gp_report_build(&s, 42u, r);
    CHECK_EQ(r[0], 0x01); CHECK_EQ(r[1], 0x80); CHECK_EQ(r[2], 3);
    CHECK_EQ((int8_t)r[3], -127); CHECK_EQ((int8_t)r[4], 127);
    CHECK_EQ(r[5], 0); CHECK_EQ((int8_t)r[6], -5); CHECK_EQ(r[7], 42);
}

static void test_clamps(void)
{
    gp_state_t s = { 0u, 200u, { -128, 0, 0, 0 } };
    uint8_t r[GP_REPORT_SIZE];
    gp_report_build(&s, 0u, r);
    CHECK_EQ(r[2], GP_HAT_NULL);
    CHECK_EQ((int8_t)r[3], -127);
}

static void test_scheduler(void)
{
    gp_tx_t tx; gp_state_t s = { 0u, GP_HAT_NULL, { 0, 0, 0, 0 } };
    uint8_t r[GP_REPORT_SIZE];
    gp_tx_init(&tx);
    CHECK(gp_tx_step(&tx, &s, 0u, true, r));        /* first report always */
    CHECK(!gp_tx_step(&tx, &s, 0u, true, r));       /* unchanged, idle 0 */
    s.buttons = 1u;
    CHECK(!gp_tx_step(&tx, &s, 0u, false, r));      /* endpoint busy */
    CHECK(gp_tx_step(&tx, &s, 0u, true, r));        /* sent once free */
    CHECK_EQ(r[0], 1); CHECK_EQ(r[7], 1);
    /* idle 2 (8 ms): resend after 8 ticks */
    int sent = 0;
    for (int i = 0; i < 8; i++) sent += gp_tx_step(&tx, &s, 2u, true, r) ? 1 : 0;
    CHECK_EQ(sent, 1);
}

int main(void)
{
    test_layout(); test_clamps(); test_scheduler();
    return check_done("test_report");
}
