
#include <stdint.h>
#include "nrf_nvic.h"

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

#if DEBUG
void uart_write(char *s);

void Default_Handler(void) {
    uart_write("Default_Handler\r\n");
    NRF_POWER->GPREGRET = 2;
    sd_nvic_SystemReset();
}

void HardFault_Handler(void) {
    uart_write("HardFault_Handler\r\n");
    NRF_POWER->GPREGRET = 2;
    sd_nvic_SystemReset();
}
#else
void Default_Handler(void) {
    NRF_POWER->GPREGRET = 2;
    sd_nvic_SystemReset();
}
void HardFault_Handler (void) __attribute__ ((weak, alias("Default_Handler")));
#endif


void Reset_Handler(void);
// Cortex-M4 interrupts.
void NMI_Handler              (void) __attribute__ ((weak, alias("HardFault_Handler")));
void MemoryManagement_Handler (void) __attribute__ ((weak, alias("HardFault_Handler")));
void BusFault_Handler         (void) __attribute__ ((weak, alias("HardFault_Handler")));
void UsageFault_Handler       (void) __attribute__ ((weak, alias("HardFault_Handler")));
void SVC_Handler              (void) __attribute__ ((weak, alias("Default_Handler")));
void DebugMon_Handler         (void) __attribute__ ((weak, alias("Default_Handler")));
void PendSV_Handler           (void) __attribute__ ((weak, alias("Default_Handler")));
void SysTick_Handler          (void) __attribute__ ((weak, alias("Default_Handler")));

void _start(void);

void Reset_Handler(void) {
    // Initialize .data segment.
    uint32_t * p_src  = &_sidata;
    uint32_t * p_dest = &_sdata;
    while (p_dest < &_edata) {
      *p_dest++ = *p_src++;
    }

    // Initialize .bss segment.
    uint32_t * p_bss     = &_sbss;
    uint32_t * p_bss_end = &_ebss;
    while (p_bss < p_bss_end) {
        *p_bss++ = 0ul;
    }

    _start();
}


const uintptr_t __Vectors[] __attribute__ ((section(".isr_vector"),used)) = {
    (uintptr_t)&_estack,
    (uintptr_t)Reset_Handler,
    (uintptr_t)NMI_Handler,
    (uintptr_t)HardFault_Handler,
    // Dirty hack to save space: the following IRQs aren't used by the
    // bootloader so we can put anything in this space. It saves 152
    // bytes (depending on the chip).
};
