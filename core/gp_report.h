/*
 * gp_report.h - gamepad state and its 8-byte USB HID input report.
 *
 * Report layout (matches the report descriptor in usb_desc.c):
 *   byte 0-1  buttons 1..16, bit 0 of byte 0 is button 1
 *   byte 2    bits 0-3 hat switch 0..7 (8 = centred / null), bits 4-7 zero
 *   byte 3    X   (left stick,  -127..127)
 *   byte 4    Y   (left stick,  -127..127, positive = down)
 *   byte 5    Z   (right stick, -127..127)
 *   byte 6    Rz  (right stick, -127..127, positive = down)
 *   byte 7    vendor-defined sequence counter
 */
#ifndef GP_REPORT_H
#define GP_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#define GP_REPORT_SIZE 8U
#define GP_REPORT_PAYLOAD 7U
#define GP_NUM_BUTTONS 16U
#define GP_NUM_AXES 4U
#define GP_HAT_NULL 8U
#define GP_AXIS_MAX 127

/* Keepalive: the unchanged state is re-sent at least this often, even when
 * the host asked for SET_IDLE 0. Linux discards reports for ~50 ms after an
 * application opens the device (usbhid HID_RESUME_RUNNING); without a
 * keepalive a state queued before the open is lost until it changes.
 * Found by the Docker USB/IP simulation; see specs/UsbHid.tla (Keepalive). */
#define GP_TX_KEEPALIVE_MS 50U

typedef struct
{
    uint16_t buttons;
    uint8_t hat;
    int8_t axis[GP_NUM_AXES];
} gp_state_t;

/* Pack a state into a report. A hat value above 7 is sent as null. */
void gp_report_build(const gp_state_t *state, uint8_t seq, uint8_t out[GP_REPORT_SIZE]);

/* Unpack a report into a state (used by tests and the simulator). */
void gp_report_parse(const uint8_t in[GP_REPORT_SIZE], gp_state_t *state);

/* Transmit scheduler: decides when a new report goes out on EP1 IN. */
typedef struct
{
    uint8_t last[GP_REPORT_SIZE];
    uint16_t since_ms;
    uint8_t seq;
    bool have_last;
} gp_tx_t;

void gp_tx_init(gp_tx_t *tx);

/*
 * Call once per millisecond. Returns true and fills `out` when a report
 * must be sent: the payload changed, or the repeat period expired. The
 * repeat period is the SET_IDLE rate (idle_4ms, in 4 ms units) capped at
 * GP_TX_KEEPALIVE_MS; idle 0 means "keepalive only".
 * When ep_ready is false nothing is consumed and false is returned.
 */
bool gp_tx_step(gp_tx_t *tx, const gp_state_t *state, uint8_t idle_4ms, bool ep_ready,
                uint8_t out[GP_REPORT_SIZE]);

#endif /* GP_REPORT_H */
