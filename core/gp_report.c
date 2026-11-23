/*
 * gp_report.c - pack and schedule gamepad HID input reports.
 */
#include "gp_report.h"

void gp_report_build(const gp_state_t *state, uint8_t seq, uint8_t out[GP_REPORT_SIZE])
{
    uint8_t hat = state->hat;
    uint32_t i;

    if (hat > 7U)
    {
        hat = (uint8_t)GP_HAT_NULL;
    }
    out[0] = (uint8_t)(state->buttons & 0xFFU);
    out[1] = (uint8_t)((uint32_t)state->buttons >> 8U);
    out[2] = (uint8_t)(hat & 0x0FU);
    for (i = 0U; i < GP_NUM_AXES; i++)
    {
        int8_t v = state->axis[i];
        if (v < -GP_AXIS_MAX)
        {
            v = (int8_t)(-GP_AXIS_MAX);
        }
        out[3U + i] = (uint8_t)v;
    }
    out[7] = seq;
}

void gp_report_parse(const uint8_t in[GP_REPORT_SIZE], gp_state_t *state)
{
    uint32_t i;

    state->buttons = (uint16_t)((uint32_t)in[0] | ((uint32_t)in[1] << 8U));
    state->hat = (uint8_t)(in[2] & 0x0FU);
    for (i = 0U; i < GP_NUM_AXES; i++)
    {
        state->axis[i] = (int8_t)in[3U + i];
    }
}

void gp_tx_init(gp_tx_t *tx)
{
    uint32_t i;

    for (i = 0U; i < GP_REPORT_SIZE; i++)
    {
        tx->last[i] = 0U;
    }
    tx->since_ms = 0U;
    tx->seq = 0U;
    tx->have_last = false;
}

static bool payload_differs(const uint8_t a[GP_REPORT_SIZE], const uint8_t b[GP_REPORT_SIZE])
{
    bool differs = false;
    uint32_t i;

    for (i = 0U; i < GP_REPORT_PAYLOAD; i++)
    {
        if (a[i] != b[i])
        {
            differs = true;
        }
    }
    return differs;
}

bool gp_tx_step(gp_tx_t *tx, const gp_state_t *state, uint8_t idle_4ms, bool ep_ready,
                uint8_t out[GP_REPORT_SIZE])
{
    uint8_t candidate[GP_REPORT_SIZE];
    bool send = false;
    uint32_t i;

    if (tx->since_ms < 0xFFFFU)
    {
        tx->since_ms++;
    }
    if (!ep_ready)
    {
        return false;
    }
    gp_report_build(state, tx->seq, candidate);
    if ((!tx->have_last) || payload_differs(candidate, tx->last))
    {
        send = true;
    }
    else if ((idle_4ms != 0U) && (tx->since_ms >= ((uint16_t)idle_4ms * 4U)))
    {
        send = true;
    }
    else
    {
        send = false;
    }
    if (send)
    {
        for (i = 0U; i < GP_REPORT_SIZE; i++)
        {
            out[i] = candidate[i];
            tx->last[i] = candidate[i];
        }
        tx->seq++;
        tx->since_ms = 0U;
        tx->have_last = true;
    }
    return send;
}
