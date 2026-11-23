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

/* SET_IDLE 0 still gets a keepalive: Linux drops reports for ~50 ms after
 * an open, so an unchanged state must be repeated (specs/UsbHid.tla). */
static void test_keepalive(void)
{
    gp_tx_t tx; gp_state_t s = { 4u, GP_HAT_NULL, { 0, 0, 0, 0 } };
    uint8_t r[GP_REPORT_SIZE];
    int sent = 0;
    gp_tx_init(&tx);
    CHECK(gp_tx_step(&tx, &s, 0u, true, r));
    for (unsigned i = 1; i < GP_TX_KEEPALIVE_MS; i++) sent += gp_tx_step(&tx, &s, 0u, true, r) ? 1 : 0;
    CHECK_EQ(sent, 0);                                  /* silent inside the period */
    CHECK(gp_tx_step(&tx, &s, 0u, true, r));            /* repeated at the keepalive */
    CHECK_EQ(r[0], 4);
    sent = 0;
    for (unsigned i = 0; i < 1000u; i++) sent += gp_tx_step(&tx, &s, 0u, true, r) ? 1 : 0;
    CHECK_EQ(sent, 1000 / GP_TX_KEEPALIVE_MS);          /* steady 20 Hz, no more */
    /* a host idle rate shorter than the keepalive wins: idle 2 = 8 ms */
    sent = 0;
    for (unsigned i = 0; i < 80u; i++) sent += gp_tx_step(&tx, &s, 2u, true, r) ? 1 : 0;
    CHECK_EQ(sent, 10);
    /* while the host is not polling the report waits; nothing is consumed */
    for (unsigned i = 0; i < 200u; i++) CHECK(!gp_tx_step(&tx, &s, 0u, false, r));
    CHECK(gp_tx_step(&tx, &s, 0u, true, r));            /* overdue: sent at once */
}

int main(void)
{
    test_layout(); test_clamps(); test_scheduler(); test_keepalive();
    return check_done("test_report");
}
