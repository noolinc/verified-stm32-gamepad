/*
 * gp_input.h - raw input conditioning: button/d-pad debounce, stick
 * deadzone and scaling. Integer only; called once per millisecond.
 */
#ifndef GP_INPUT_H
#define GP_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "gp_report.h"

/* Raw input bits: 0..15 buttons, 16..19 d-pad up/down/left/right. */
#define GP_IN_UP ((uint32_t)1U << 16U)
#define GP_IN_DOWN ((uint32_t)1U << 17U)
#define GP_IN_LEFT ((uint32_t)1U << 18U)
#define GP_IN_RIGHT ((uint32_t)1U << 19U)
#define GP_NUM_INPUTS 20U
#define GP_DEBOUNCE_MS 5U
#define GP_ADC_MAX 4095U

typedef struct
{
    uint32_t inputs;                /* 1 = pressed */
    uint16_t axis_raw[GP_NUM_AXES]; /* 12-bit ADC counts */
} gp_raw_t;

typedef struct
{
    uint16_t center;
    uint16_t deadzone;
    bool invert;
} gp_axis_cal_t;

typedef struct
{
    uint8_t count[GP_NUM_INPUTS];
    uint32_t stable;
    gp_axis_cal_t cal[GP_NUM_AXES];
} gp_input_t;

/* Reset debounce state and load default calibration (centre 2048, deadzone 96). */
void gp_input_init(gp_input_t *ctx);

/* Advance one millisecond and produce the conditioned state. */
void gp_input_step(gp_input_t *ctx, const gp_raw_t *raw, gp_state_t *out);

/* Map a 12-bit reading to -127..127 around a centre with a deadzone. */
int8_t gp_axis_scale(uint16_t raw, const gp_axis_cal_t *cal);

/* D-pad bits (GP_IN_UP..GP_IN_RIGHT) to a hat value 0..7, or GP_HAT_NULL. */
uint8_t gp_hat_from_dpad(uint32_t inputs);

#endif /* GP_INPUT_H */
