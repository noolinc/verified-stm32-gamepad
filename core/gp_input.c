/*
 * gp_input.c - debounce and stick conditioning.
 *
 * Debounce is a saturating integrator per input: a press must be seen for
 * GP_DEBOUNCE_MS consecutive ticks (net) before it is reported, and a
 * release likewise, so contact bounce shorter than the window is ignored.
 */
#include "gp_input.h"

#define DEFAULT_CENTER 2048U
#define DEFAULT_DEADZONE 96U

void gp_input_init(gp_input_t *ctx)
{
    uint32_t i;

    for (i = 0U; i < GP_NUM_INPUTS; i++)
    {
        ctx->count[i] = 0U;
    }
    ctx->stable = 0U;
    for (i = 0U; i < GP_NUM_AXES; i++)
    {
        ctx->cal[i].center = (uint16_t)DEFAULT_CENTER;
        ctx->cal[i].deadzone = (uint16_t)DEFAULT_DEADZONE;
        ctx->cal[i].invert = false;
    }
}

static void debounce(gp_input_t *ctx, uint32_t inputs)
{
    uint32_t i;

    for (i = 0U; i < GP_NUM_INPUTS; i++)
    {
        uint32_t mask = (uint32_t)1U << i;
        if ((inputs & mask) != 0U)
        {
            if (ctx->count[i] < GP_DEBOUNCE_MS)
            {
                ctx->count[i]++;
            }
        }
        else if (ctx->count[i] > 0U)
        {
            ctx->count[i]--;
        }
        else
        {
            /* already released */
        }
        if (ctx->count[i] >= GP_DEBOUNCE_MS)
        {
            ctx->stable |= mask;
        }
        else if (ctx->count[i] == 0U)
        {
            ctx->stable &= ~mask;
        }
        else
        {
            /* inside the window: keep the last stable value */
        }
    }
}

int8_t gp_axis_scale(uint16_t raw, const gp_axis_cal_t *cal)
{
    int32_t center = (int32_t)cal->center;
    int32_t dz = (int32_t)cal->deadzone;
    int32_t d = (int32_t)raw - center;
    int32_t mag = (d < 0) ? -d : d;
    int32_t span = (d < 0) ? center : ((int32_t)GP_ADC_MAX - center);
    int32_t scaled;

    if ((mag <= dz) || (span <= dz))
    {
        return 0;
    }
    scaled = ((mag - dz) * GP_AXIS_MAX) / (span - dz);
    if (scaled > GP_AXIS_MAX)
    {
        scaled = GP_AXIS_MAX;
    }
    if (d < 0)
    {
        scaled = -scaled;
    }
    if (cal->invert)
    {
        scaled = -scaled;
    }
    return (int8_t)scaled;
}

uint8_t gp_hat_from_dpad(uint32_t inputs)
{
    /* Index: bit0 up, bit1 down, bit2 left, bit3 right. Opposites cancel. */
    static const uint8_t table[16] = {
        8U, 0U, 4U, 8U, 6U, 7U, 5U, 6U, 2U, 1U, 3U, 2U, 8U, 0U, 4U, 8U,
    };
    uint32_t idx = (inputs >> 16U) & 0x0FU;

    return table[idx];
}

void gp_input_step(gp_input_t *ctx, const gp_raw_t *raw, gp_state_t *out)
{
    uint32_t i;

    debounce(ctx, raw->inputs);
    out->buttons = (uint16_t)(ctx->stable & 0xFFFFU);
    out->hat = gp_hat_from_dpad(ctx->stable);
    for (i = 0U; i < GP_NUM_AXES; i++)
    {
        out->axis[i] = gp_axis_scale(raw->axis_raw[i], &ctx->cal[i]);
    }
}
