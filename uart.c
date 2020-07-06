
#include "nrf_soc.h"
#include "dfu.h"

void uart_write_char(char ch) {
    NRF_UART0->TXD = ch;
    while (NRF_UART0->EVENTS_TXDRDY != 1) {}
    NRF_UART0->EVENTS_TXDRDY = 0;
}

void uart_write(char *s) {
    while (*s) {
        uart_write_char(*s++);
    }
}

void uart_write_num(uint32_t n) {
    uart_write_char('0');
    uart_write_char('x');

    // write hex digits
    for (int i = 0; i < 8; i++) {
        char ch = (n >> 28) + '0';
        if (ch > '9') {
            ch = (n >> 28) + 'a' - 10;
        }
        uart_write_char(ch);
        n <<= 4;
    }
}

#if DEBUG
void uart_enable(void) {
    // TODO: set correct GPIO configuration? Only necessary when system
    // goes to OFF state.
    NRF_UART0->ENABLE        = UART_ENABLE_ENABLE_Enabled;
    NRF_UART0->BAUDRATE      = UART_BAUDRATE_BAUDRATE_Baud115200;
    NRF_UART0->TASKS_STARTTX = 1;
    #if defined(PCA10040)
    NRF_UART0->PSELTXD       = 6; // P0.06
    #elif defined(PCA10056)
    NRF_UART0->PSELTXD       = 6; // P0.06
    #else
    #error Setup TX pin for debugging
    #endif
}
#endif

void uart_disable(void) {
    NRF_UART0->ENABLE  = UART_ENABLE_ENABLE_Disabled;
    NRF_UART0->PSELTXD = 0xffffffff;
}
