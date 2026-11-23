/*
 * hid_parse.h - tiny HID report-descriptor parser for tests: walks short
 * items, tracks the global/local state and lists every Input field the way
 * a host (macOS IOHIDFamily) would lay out the report.
 */
#ifndef HID_PARSE_H
#define HID_PARSE_H

#include <stdint.h>
#include <string.h>

typedef struct {
    uint16_t usage_page;
    uint16_t usage;      /* 0 for padding */
    uint32_t bit_offset;
    uint32_t bit_size;
    int32_t logical_min;
    int32_t logical_max;
    int is_const;
    int has_null;
} hid_field_t;

typedef struct {
    hid_field_t fields[64];
    int nfields;
    uint32_t total_bits;
    int collections_open;
    int max_depth;
    uint16_t app_usage_page;
    uint16_t app_usage;
    int error;           /* nonzero = malformed descriptor */
} hid_layout_t;

static inline int32_t hid_sval(const uint8_t *d, int n)
{
    if (n == 1) return (int8_t)d[0];
    if (n == 2) return (int16_t)(d[0] | (d[1] << 8));
    if (n == 4) return (int32_t)(d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24));
    return 0;
}
static inline uint32_t hid_uval(const uint8_t *d, int n)
{
    if (n == 1) return d[0];
    if (n == 2) return (uint32_t)(d[0] | (d[1] << 8));
    if (n == 4) return (uint32_t)(d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24));
    return 0;
}

static inline void hid_parse(const uint8_t *d, int len, hid_layout_t *out)
{
    uint16_t page = 0; int32_t lmin = 0, lmax = 0; uint32_t rsize = 0, rcount = 0;
    uint16_t usages[32]; int nus = 0; uint32_t umin = 0, umax = 0; int have_range = 0;
    int i = 0;
    memset(out, 0, sizeof *out);
    while (i < len) {
        uint8_t pre = d[i];
        int sz = pre & 3; if (sz == 3) sz = 4;
        int type = (pre >> 2) & 3, tag = (pre >> 4) & 0xF;
        if (i + 1 + sz > len) { out->error = 1; return; }
        const uint8_t *v = &d[i + 1];
        if (type == 1) {               /* global */
            if (tag == 0) page = (uint16_t)hid_uval(v, sz);
            else if (tag == 1) lmin = hid_sval(v, sz);
            else if (tag == 2) lmax = (lmin < 0) ? hid_sval(v, sz) : (int32_t)hid_uval(v, sz);
            else if (tag == 7) rsize = hid_uval(v, sz);
            else if (tag == 9) rcount = hid_uval(v, sz);
        } else if (type == 2) {        /* local */
            if (tag == 0 && nus < 32) usages[nus++] = (uint16_t)hid_uval(v, sz);
            else if (tag == 1) { umin = hid_uval(v, sz); have_range = 1; }
            else if (tag == 2) { umax = hid_uval(v, sz); have_range = 1; }
        } else if (type == 0) {        /* main */
            if (tag == 0xA) {          /* collection */
                if (out->collections_open == 0 && nus > 0) {
                    out->app_usage_page = page; out->app_usage = usages[0];
                }
                out->collections_open++;
                if (out->collections_open > out->max_depth) out->max_depth = out->collections_open;
            } else if (tag == 0xC) {   /* end collection */
                out->collections_open--;
                if (out->collections_open < 0) { out->error = 2; return; }
            } else if (tag == 0x8) {   /* input */
                uint32_t flags = hid_uval(v, sz);
                for (uint32_t k = 0; k < rcount && out->nfields < 64; k++) {
                    hid_field_t *f = &out->fields[out->nfields++];
                    f->usage_page = page;
                    if (flags & 1) f->usage = 0;
                    else if (have_range) f->usage = (uint16_t)(umin + k <= umax ? umin + k : umax);
                    else if (nus > 0) f->usage = usages[(int)k < nus ? (int)k : nus - 1];
                    f->bit_offset = out->total_bits; f->bit_size = rsize;
                    f->logical_min = lmin; f->logical_max = lmax;
                    f->is_const = (int)(flags & 1); f->has_null = (flags & 0x40) != 0;
                    out->total_bits += rsize;
                }
            }
            nus = 0; have_range = 0; umin = umax = 0;  /* locals reset after main item */
        }
        i += 1 + sz;
    }
    if (out->collections_open != 0) out->error = 3;
}

/* Extract a field's value from a report (little-endian bit packing). */
static inline int32_t hid_extract(const uint8_t *report, const hid_field_t *f)
{
    uint32_t v = 0;
    for (uint32_t b = 0; b < f->bit_size; b++) {
        uint32_t bit = f->bit_offset + b;
        if (report[bit / 8] & (1u << (bit % 8))) v |= 1u << b;
    }
    if (f->logical_min < 0 && f->bit_size < 32 && (v & (1u << (f->bit_size - 1)))) {
        v |= ~((1u << f->bit_size) - 1u);
    }
    return (int32_t)v;
}

#endif
