/* Scripted enumeration in the order macOS performs it. */
#include "check.h"
#include "usb_fake.h"
#include "usb_desc.h"

static void test_enumeration(void)
{
    usb_fake_reset();
    CHECK(usb_fake_setup(0x80, 6, 0x0100, 0, 64));      /* device descriptor, first 64 */
    CHECK_EQ(g_fake.len, 18); CHECK(!g_fake.zlp);
    CHECK(usb_fake_setup(0x00, 5, 7, 0, 0));            /* SET_ADDRESS 7 */
    CHECK_EQ(g_fake.address, 7); CHECK_EQ(usb_ctrl_state(), USB_STATE_ADDRESS);
    CHECK(usb_fake_setup(0x80, 6, 0x0100, 0, 18));
    CHECK(usb_fake_setup(0x80, 6, 0x0200, 0, 9));       /* config header */
    CHECK_EQ(g_fake.len, 9);
    CHECK(usb_fake_setup(0x80, 6, 0x0200, 0, 255));     /* full config */
    CHECK_EQ(g_fake.len, 34);
    CHECK(usb_fake_setup(0x80, 6, 0x0300, 0, 255));     /* languages */
    CHECK(usb_fake_setup(0x80, 6, 0x0302, 0x0409, 255));/* product */
    CHECK(!usb_fake_setup(0x80, 6, 0x0600, 0, 10));     /* qualifier: stall (FS only) */
    CHECK(usb_fake_setup(0x00, 9, 1, 0, 0));            /* SET_CONFIGURATION 1 */
    CHECK_EQ(g_fake.config, 1); CHECK_EQ(usb_ctrl_state(), USB_STATE_CONFIGURED);
    CHECK(usb_fake_setup(0x21, 0x0A, 0, 0, 0));         /* SET_IDLE 0 */
    CHECK(usb_fake_setup(0x81, 6, 0x2200, 0, 0x1000));  /* report descriptor */
    CHECK_EQ(g_fake.len, usb_desc_report().len);
    g_fake.report[0] = 0x5A;
    CHECK(usb_fake_setup(0xA1, 0x01, 0x0100, 0, 8));    /* GET_REPORT input */
    CHECK_EQ(g_fake.len, 8); CHECK_EQ(g_fake.data[0], 0x5A);
}

static void test_edge_cases(void)
{
    usb_fake_reset();
    CHECK(!usb_fake_setup(0x00, 9, 1, 0, 0));           /* configure before address: stall */
    CHECK(!usb_fake_setup(0x00, 5, 200, 0, 0));         /* address > 127 */
    CHECK(!usb_fake_setup(0x21, 0x09, 0x0200, 0, 1));   /* SET_REPORT: no output reports */
    CHECK(!usb_fake_setup(0x40, 1, 0, 0, 0));           /* vendor request */
    CHECK(usb_fake_setup(0x21, 0x0A, 0x7D00, 0, 0));    /* SET_IDLE 125*4 ms */
    CHECK_EQ(usb_ctrl_idle_rate(), 125);
    CHECK(usb_fake_setup(0x80, 6, 0x0200, 0, 34));      /* exactly wLength: no ZLP */
    CHECK(!g_fake.zlp);
    usb_ctrl_bus_reset();
    CHECK_EQ(usb_ctrl_state(), USB_STATE_DEFAULT); CHECK_EQ(usb_ctrl_idle_rate(), 0);
}

int main(void)
{
    test_enumeration(); test_edge_cases();
    return check_done("test_usb_ctrl");
}
