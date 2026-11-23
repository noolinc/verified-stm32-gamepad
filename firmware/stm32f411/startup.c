/*
 * startup.c - vector table and reset handler (no assembly).
 */
#include <stdint.h>

#include "regs.h"

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

extern int main(void);
void Reset_Handler(void);
void Default_Handler(void);
void SysTick_Handler(void);

typedef struct
{
    uint32_t *initial_sp;
    void (*handlers[15])(void);
} vector_table_t;

__attribute__((section(".isr_vector"), used)) const vector_table_t g_vectors = {
    &_estack,
    {
        Reset_Handler,   /* Reset */
        Default_Handler, /* NMI */
        Default_Handler, /* HardFault */
        Default_Handler, /* MemManage */
        Default_Handler, /* BusFault */
        Default_Handler, /* UsageFault */
        0, 0, 0, 0,      /* reserved */
        Default_Handler, /* SVCall */
        Default_Handler, /* DebugMon */
        0,               /* reserved */
        Default_Handler, /* PendSV */
        SysTick_Handler, /* SysTick */
    },
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    while (dst < &_edata)
    {
        *dst = *src;
        dst++;
        src++;
    }
    dst = &_sbss;
    while (dst < &_ebss)
    {
        *dst = 0U;
        dst++;
    }
    SCB_CPACR |= (0xFUL << 20U); /* CP10/CP11 full access: hard-float ABI */
    (void)main();
    for (;;)
    {
        /* main never returns */
    }
}

void Default_Handler(void)
{
    for (;;)
    {
        /* unexpected exception: halt for the debugger */
    }
}
