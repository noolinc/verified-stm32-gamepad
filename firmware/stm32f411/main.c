/*
 * main.c - superloop: USB is serviced continuously; inputs are sampled,
 * debounced and reported once per millisecond.
 */
#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "clock.h"
#include "gp_input.h"
#include "gp_report.h"
#include "usb_ctrl.h"
#include "usb_desc.h"
#include "usb_otg_fs.h"

#define LED_BLINK_MS 250U

static gp_input_t s_input;
static gp_state_t s_state;
static gp_tx_t s_tx;

static void current_report(uint8_t out[GP_REPORT_SIZE])
{
    gp_report_build(&s_state, s_tx.seq, out);
}

/* No crystal or USB core: blink the LED forever. */
static void fault_halt(void)
{
    bool on = false;

    tick_init();
    for (;;)
    {
        if (tick_take() != 0U)
        {
            static uint32_t ms;
            ms++;
            if (ms >= LED_BLINK_MS)
            {
                ms = 0U;
                on = !on;
                board_led(on);
            }
        }
    }
}

static void app_tick(void)
{
    gp_raw_t raw;
    uint8_t report[GP_REPORT_SIZE];

    if (usb_take_link_change())
    {
        gp_tx_init(&s_tx); /* specs/UsbHid.tla: ReinitScheduler */
    }
    board_read(&raw);
    gp_input_step(&s_input, &raw, &s_state);
    if (gp_tx_step(&s_tx, &s_state, usb_ctrl_idle_rate(), usb_ep1_ready(), report))
    {
        usb_ep1_send(report);
    }
    board_led(usb_ctrl_state() == USB_STATE_CONFIGURED);
}

int main(void)
{
    uint8_t uid[USB_UID_BYTES];

    board_init();
    if (!clock_init())
    {
        fault_halt();
    }
    board_uid(uid);
    usb_desc_set_serial(uid);
    gp_input_init(&s_input);
    gp_tx_init(&s_tx);
    s_state.hat = (uint8_t)GP_HAT_NULL;
    s_input.cal[1].invert = false;
    if (!usb_init(current_report))
    {
        fault_halt();
    }
    tick_init();
    for (;;)
    {
        usb_poll();
        if (tick_take() != 0U)
        {
            app_tick();
        }
    }
}
