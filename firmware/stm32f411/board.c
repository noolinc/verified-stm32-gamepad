/*
 * board.c - GPIO buttons, ADC sticks, LED, unique ID.
 */
#include "board.h"

#include "regs.h"

#define ADC_TIMEOUT 2000UL
#define ADC_CHANNELS 4U

typedef struct
{
    uint32_t port;
    uint8_t pin;
} pin_t;

static const pin_t s_buttons[GP_NUM_BUTTONS] = {
    { GPIOB_BASE, 0U },  { GPIOB_BASE, 1U },  { GPIOB_BASE, 3U },  { GPIOB_BASE, 4U },
    { GPIOB_BASE, 5U },  { GPIOB_BASE, 6U },  { GPIOB_BASE, 7U },  { GPIOB_BASE, 8U },
    { GPIOB_BASE, 9U },  { GPIOB_BASE, 10U }, { GPIOB_BASE, 12U }, { GPIOB_BASE, 13U },
    { GPIOB_BASE, 14U }, { GPIOB_BASE, 15U }, { GPIOA_BASE, 8U },  { GPIOA_BASE, 15U },
};

static const pin_t s_dpad[4] = {
    { GPIOA_BASE, 9U }, { GPIOA_BASE, 10U }, { GPIOC_BASE, 14U }, { GPIOC_BASE, 15U },
};

static const uint8_t s_adc_channel[ADC_CHANNELS] = { 1U, 2U, 3U, 4U };

static void input_pullup(const pin_t *p)
{
    uint32_t shift = 2UL * p->pin;

    GPIO_MODER(p->port) &= ~(3UL << shift);
    GPIO_PUPDR(p->port) = (GPIO_PUPDR(p->port) & ~(3UL << shift)) | (1UL << shift);
}

static void adc_init(void)
{
    uint32_t i;

    RCC_APB2ENR |= (1UL << 8U); /* ADC1EN */
    ADC_CCR = (1UL << 16U);     /* ADCCLK = PCLK2 / 4 = 24 MHz */
    for (i = 0U; i < ADC_CHANNELS; i++)
    {
        uint32_t shift = 2UL * s_adc_channel[i];
        GPIO_MODER(GPIOA_BASE) |= (3UL << shift); /* analog */
        ADC1_SMPR2 |= (4UL << (3UL * s_adc_channel[i])); /* 84 cycles */
    }
    ADC1_SQR1 = 0U; /* one conversion per start */
    ADC1_CR2 = 1U;  /* ADON */
}

void board_init(void)
{
    uint32_t i;

    RCC_AHB1ENR |= 7UL; /* GPIOA, GPIOB, GPIOC */
    for (i = 0U; i < GP_NUM_BUTTONS; i++)
    {
        input_pullup(&s_buttons[i]);
    }
    for (i = 0U; i < 4U; i++)
    {
        input_pullup(&s_dpad[i]);
    }
    GPIO_MODER(GPIOC_BASE) = (GPIO_MODER(GPIOC_BASE) & ~(3UL << 26U)) | (1UL << 26U); /* PC13 out */
    board_led(false);
    adc_init();
}

static uint16_t adc_read(uint8_t channel)
{
    uint32_t n;

    ADC1_SQR3 = channel;
    ADC1_CR2 |= (1UL << 30U); /* SWSTART */
    for (n = 0U; n < ADC_TIMEOUT; n++)
    {
        if ((ADC1_SR & 2UL) != 0U)
        {
            return (uint16_t)(ADC1_DR & 0x0FFFUL);
        }
    }
    return (uint16_t)(GP_ADC_MAX / 2U); /* timed out: report centre */
}

static bool pressed(const pin_t *p)
{
    return (GPIO_IDR(p->port) & (1UL << p->pin)) == 0U;
}

void board_read(gp_raw_t *raw)
{
    uint32_t bits = 0U;
    uint32_t i;

    for (i = 0U; i < GP_NUM_BUTTONS; i++)
    {
        if (pressed(&s_buttons[i]))
        {
            bits |= (1UL << i);
        }
    }
    for (i = 0U; i < 4U; i++)
    {
        if (pressed(&s_dpad[i]))
        {
            bits |= (1UL << (16U + i));
        }
    }
    raw->inputs = bits;
    for (i = 0U; i < ADC_CHANNELS; i++)
    {
        raw->axis_raw[i] = adc_read(s_adc_channel[i]);
    }
}

void board_led(bool on)
{
    GPIO_BSRR(GPIOC_BASE) = on ? (1UL << (13U + 16U)) : (1UL << 13U);
}

void board_uid(uint8_t uid[USB_UID_BYTES])
{
    uint32_t i;

    for (i = 0U; i < USB_UID_BYTES; i++)
    {
        uid[i] = (uint8_t)(REG32(UID_BASE + (4UL * (i / 4U))) >> (8U * (i % 4U)));
    }
}
