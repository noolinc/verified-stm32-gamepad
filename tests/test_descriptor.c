#include "check.h"
#include "hid_parse.h"
#include "usb_desc.h"
#include "gp_report.h"

static void test_report_descriptor(void)
{
    usb_buf_t r = usb_desc_report();
    hid_layout_t L;
    hid_parse(r.data, r.len, &L);
    CHECK_EQ(L.error, 0);
    CHECK_EQ(L.app_usage_page, 0x01); CHECK_EQ(L.app_usage, 0x05);   /* Game Pad */
    CHECK_EQ(L.total_bits, GP_REPORT_SIZE * 8u);
    /* buttons 1..16 at bits 0..15 */
    for (int i = 0; i < 16; i++) {
        CHECK_EQ(L.fields[i].usage_page, 0x09); CHECK_EQ(L.fields[i].usage, i + 1);
        CHECK_EQ(L.fields[i].bit_offset, (uint32_t)i);
    }
    CHECK_EQ(L.fields[16].usage, 0x39); CHECK(L.fields[16].has_null); CHECK_EQ(L.fields[16].bit_size, 4);
    CHECK(L.fields[17].is_const);
    const uint16_t axes[4] = { 0x30, 0x31, 0x32, 0x35 };
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(L.fields[18 + i].usage, axes[i]);
        CHECK_EQ(L.fields[18 + i].bit_offset, 24u + 8u * (uint32_t)i);
        CHECK_EQ(L.fields[18 + i].logical_min, -127); CHECK_EQ(L.fields[18 + i].logical_max, 127);
    }
    CHECK_EQ(L.fields[22].usage_page, 0xFF00);
}

static void test_config_descriptor(void)
{
    usb_buf_t c = usb_desc_config(), d = usb_desc_device(), h = usb_desc_hid(), r = usb_desc_report();
    CHECK_EQ(d.len, 18); CHECK_EQ(d.data[0], 18); CHECK_EQ(d.data[7], 64);
    CHECK_EQ(c.data[2] | (c.data[3] << 8), c.len);
    /* walk descriptors: lengths must tile the config exactly */
    int off = 0, n = 0;
    while (off < c.len && n < 10) { CHECK(c.data[off] > 0); off += c.data[off]; n++; }
    CHECK_EQ(off, c.len); CHECK_EQ(n, 4);
    CHECK_EQ(h.data[1], 0x21); CHECK_EQ(h.data[7] | (h.data[8] << 8), r.len);
    CHECK_EQ(c.data[27 + 2], 0x81); CHECK_EQ(c.data[27 + 4], GP_REPORT_SIZE);
}

static void test_strings(void)
{
    const uint8_t uid[USB_UID_BYTES] = { 0xDE, 0xAD, 0xBE, 0xEF, 0, 1, 2, 3, 4, 5, 6, 0xA7 };
    usb_desc_set_serial(uid);
    usb_buf_t s = usb_desc_string(USB_STR_SERIAL);
    CHECK_EQ(s.len, 2 + 2 * 24); CHECK_EQ(s.data[2], 'D'); CHECK_EQ(s.data[46], 'A'); CHECK_EQ(s.data[48], '7'); CHECK_EQ(s.data[49], 0);
    usb_buf_t l = usb_desc_string(USB_STR_LANG);
    CHECK_EQ(l.len, 4); CHECK_EQ(l.data[2], 0x09); CHECK_EQ(l.data[3], 0x04);
    CHECK_EQ(usb_desc_string(9).len, 0);
    usb_desc_set_product("A very long product name that will be truncated!");
    CHECK_EQ(usb_desc_string(USB_STR_PRODUCT).len, 2 + 2 * 31);
    usb_desc_set_product(0);
}

int main(void)
{
    test_report_descriptor(); test_config_descriptor(); test_strings();
    return check_done("test_descriptor");
}
