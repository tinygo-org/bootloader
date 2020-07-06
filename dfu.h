
#pragma once

// Commands that can be issued for certain functionality. The main command is
// COMMAND_START, which starts the DFU process (erasing flash and receiving
// data).
enum {
    COMMAND_RESET_BOOTLOADER = 0x00, // reset into the bootloader
    COMMAND_RESET            = 0x01, // regular reset
    COMMAND_START            = 0x02, // start DFU process
    COMMAND_PING             = 0x10, // just ask a response (debug)
};

// Statuses send back via a notification on the command characteristic.
enum {
    STATUS_PONG                 = 0x01, // ping reply
    STATUS_ERASE_STARTED        = 0x02, // erase started
    STATUS_ERASE_FINISHED       = 0x03, // erase finished, client may start to stream data
    STATUS_WRITE_FINISHED       = 0x04, // write finished, firmware has been rewritten
    STATUS_BUSY                 = 0x10, // another command is still running
    STATUS_INVALID_ERASE_START  = 0x20, // invalid start address for erase command (not at APP_CODE_BASE)
    STATUS_INVALID_ERASE_LENGTH = 0x21, // invalid length for erase command (would overwrite bootloader)
    STATUS_ERASE_FAILED         = 0x30, // could not erase flash page
    STATUS_WRITE_FAILED         = 0x31, // could not write flash page
    STATUS_WRITE_TOO_FAST       = 0x32, // could not write flash page: data came in faster than could be written
};

// Now follow regular declarations shared between main.c and ble.c.

#include <stdint.h>

// Internal states for keeping track where we are in the DFU process.
enum {
    PHASE_READY,
    PHASE_ERASING,
    PHASE_WRITING,
    PHASE_WRITING_LAST_PAGE,
    PHASE_RESETTING,
};

#if DEBUG
void uart_write(char *s);
void uart_write_num(uint32_t n);
void uart_enable(void);
void uart_disable(void);
#define LOG(s)        uart_write(s "\r\n")
#define LOG_NUM(s, n) uart_write(s " "); uart_write_num(n); uart_write("\r\n")
#else
#define LOG(x)
#define LOG_NUM(s, n)
#endif

void ble_init(void);
void ble_run(void);
void ble_send_reply(uint8_t code);
void ble_disconnect(void);

extern const uint32_t _stext[];

typedef union {
    struct {
        uint8_t  command;
    } any;
    struct {
        uint8_t  command;
        uint8_t  padding[3];
        uint32_t startAddr;
        uint32_t length;
    } start; // COMMAND_START
} ble_command_t;

void handle_command(uint16_t data_len, ble_command_t *data);
void handle_data(uint16_t data_len, uint8_t *data);
void handle_disconnect(void);

void sd_evt_handler(uint32_t evt_id);
