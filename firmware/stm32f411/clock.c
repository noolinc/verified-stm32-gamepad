/*
 * clock.c - clock tree and SysTick.
 */
#include "clock.h"

#include "regs.h"

#define HSE_TIMEOUT 500000UL
#define RCC_CR_HSEON (1UL << 16U)
#define RCC_CR_HSERDY (1UL << 17U)
#define RCC_CR_PLLON (1UL << 24U)
#define RCC_CR_PLLRDY (1UL << 25U)
#define PLL_M 25UL
#define PLL_N 192UL
#define PLL_Q 4UL
#define PLL_SRC_HSE (1UL << 22U)
#define CFGR_PPRE1_DIV2 (4UL << 10U)
#define CFGR_SW_PLL 2UL
#define CFGR_SWS_MASK (3UL << 2U)
#define CFGR_SWS_PLL (2UL << 2U)
#define PWR_VOS_SCALE1 (3UL << 14U)
#define FLASH_3WS_CACHES (3UL | (1UL << 8U) | (1UL << 9U) | (1UL << 10U))

static volatile uint32_t s_ticks;

void spin(uint32_t loops)
{
    volatile uint32_t n;

    for (n = 0U; n < loops; n++)
    {
        /* delay */
    }
}

static bool wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
    uint32_t n;

    for (n = 0U; n < HSE_TIMEOUT; n++)
    {
        if ((*reg & mask) == value)
        {
            return true;
        }
    }
    return false;
}

bool clock_init(void)
{
    RCC_CR |= RCC_CR_HSEON;
    if (!wait_set(&RCC_CR, RCC_CR_HSERDY, RCC_CR_HSERDY))
    {
        return false;
    }
    RCC_APB1ENR |= (1UL << 28U); /* PWREN */
    PWR_CR |= PWR_VOS_SCALE1;
    FLASH_ACR = FLASH_3WS_CACHES;
    RCC_PLLCFGR = PLL_M | (PLL_N << 6U) | PLL_SRC_HSE | (PLL_Q << 24U); /* PLLP = /2 */
    RCC_CFGR = CFGR_PPRE1_DIV2;
    RCC_CR |= RCC_CR_PLLON;
    if (!wait_set(&RCC_CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY))
    {
        return false;
    }
    RCC_CFGR |= CFGR_SW_PLL;
    return wait_set(&RCC_CFGR, CFGR_SWS_MASK, CFGR_SWS_PLL);
}

void tick_init(void)
{
    SYST_RVR = (SYSCLK_HZ / 1000UL) - 1UL;
    SYST_CVR = 0U;
    SYST_CSR = 7U; /* processor clock, interrupt, enable */
}

void SysTick_Handler(void)
{
    s_ticks++;
}

uint32_t tick_take(void)
{
    static uint32_t s_seen;
    uint32_t now = s_ticks;
    uint32_t elapsed = now - s_seen;

    s_seen = now;
    return elapsed;
}
