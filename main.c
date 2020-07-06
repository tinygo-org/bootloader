
// This file is the main DFU. It calls into ble.c for BLE related
// functionality, and ble.c calls back to functions defined here when it
// receives BLE events.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nrf_sdm.h"
#include "nrf_mbr.h"
#include "nrf_nvic.h"

#include "dfu.h"

extern const uint32_t _stext[];

__attribute__((section(".bootloaderaddr"),used))
const uint32_t *bootloaderaddr = _stext;

#define SD_CODE_BASE     (0x00001000)
#define PAGE_SIZE        (4096)
#define MBR_VECTOR_TABLE (0x20000000)

// Read SoftDevice size from the SoftDevice information structure
// https://infocenter.nordicsemi.com/index.jsp?topic=%2Fsds_s132%2FSDS%2Fs1xx%2Fsd_info_structure%2Fsd_info_structure.html
#define APP_CODE_BASE (*(uint32_t*)(0x3008))

// A number of reset reasons that might indicate something went wrong and the
// chip should enter DFU mode.
#define DFU_RESET_REASONS (POWER_RESETREAS_RESETPIN_Msk | POWER_RESETREAS_DOG_Msk | POWER_RESETREAS_LOCKUP_Msk)

static volatile char phase = PHASE_READY;

// Globals for erase phase.
static volatile uint32_t flash_erase_current_page;
static volatile uint32_t flash_erase_last_page;

// Globals for write phase.
static          uint8_t  flash_write_buf[PAGE_SIZE * 2];
static          uint32_t flash_write_app_size; // must be aligned to 4
static          uint32_t flash_write_index;
static volatile uint32_t flash_write_current_page; // page that will be written or is currently being written

static void resume_flash_erase(void);
static void write_current_page(void);

#if DEBUG
void softdevice_assert_handler(uint32_t id, uint32_t pc, uint32_t info) {
    LOG("ERROR: SoftDevice assert!!!");
    while (1);
}
#else // no debug
void Default_Handler(void);
#define softdevice_assert_handler ((nrf_fault_handler_t)Default_Handler)
#endif

// Start running the application, by jumping to the SoftDevice. This function
// does not return.
static void jump_to_app() {
#if DEBUG
    uart_disable();
#endif
    // Note that the SoftDevice needs to be disabled before calling this
    // function.

    // The ISR vector contains these entries (among others):
    // 0: pointer to the end of the stack (_estack)
    // 1: the Reset_Handler
    // Note that we can't just jump to the app, we have to 'reset' the
    // stack pointer to the beginning of the stack (e.g. the highest
    // address).
    uint32_t *sd_isr = (uint32_t*)SD_CODE_BASE;
    uint32_t new_sp = sd_isr[0]; // load end of stack (_estack)
    uint32_t new_pc = sd_isr[1]; // load Reset_Handler
    __asm__ __volatile__(
            "mov sp, %[new_sp]\n" // set stack pointer to initial stack pointer
            "mov pc, %[new_pc]\n" // jump to SoftDevice Reset_Vector
            :
            : [new_sp]"r" (new_sp),
              [new_pc]"r" (new_pc));
    __builtin_unreachable();
}

// Entrypoint for the DFU. Called unconditionally at reset. It will determine
// whether to start the DFU or jump to the application.
void _start(void) {
#if DEBUG
    uart_enable();
#endif

    LOG("");

    // Set the vector table. This may be used by the SoftDevice.
    LOG("init MBR vector table");
    *(uint32_t*)MBR_VECTOR_TABLE = SD_CODE_BASE;

    // Check whether there is something that looks like a reset handler at
    // the app ISR vector. If the page has been cleared, it will be
    // 0xffffffff.
    // Also check for other reasons DFU may be triggered:
    //   * GPREGRET is set, which means DFU mode was requested
    //   * The reset reason is suspicious.
    uint32_t *app_isr = (uint32_t*)APP_CODE_BASE;
    uint32_t reset_handler = app_isr[1];
    if (reset_handler != 0xffffffff && NRF_POWER->GPREGRET == 0 && (NRF_POWER->RESETREAS & DFU_RESET_REASONS) == 0) {
        // There is a valid application and the application hasn't
        // requested for DFU mode.
        LOG("jump to application");
        jump_to_app();
    } else {
        LOG("DFU mode triggered");
    }

    // Clear reset reasons that we've looked at, to avoid getting stuck in
    // DFU mode.
    // The dataseet says: "A field is cleared by writing '1' to it."
    NRF_POWER->RESETREAS = DFU_RESET_REASONS;

    // Make sure a reset won't jump to the bootloader again. This only
    // matters if the application requested to go to the bootloader by
    // setting this register (it defaults to 0).
    NRF_POWER->GPREGRET = 0;

    // Try to disable the SoftDevice, if it is enabled. Sometimes it
    // appears to not be fully disabled even after a reset.
    // This adds almost no code size (4 bytes, could be shrunk to 2 bytes
    // theoretically) but makes the DFU more reliable.
    sd_softdevice_disable();

    // This always uses the internal clock. Which takes more power, but
    // DFU mode isn't meant to be enabled for long periods anyway. It
    // avoids having to configure internal/external clocks.
    LOG("enable sd");
    uint32_t err_code = sd_softdevice_enable(NULL, softdevice_assert_handler);
    if (err_code != 0) {
        LOG_NUM("cannot enable SoftDevice:", err_code);
    }

    ble_init();

    LOG("waiting...");
    ble_run();
}

// handle_command is called when the command characteristic is written by the
// client.
void handle_command(uint16_t data_len, ble_command_t *cmd) {
    // Format: command (1 byte), payload (any length, up to 19 bytes with
    // default MTU).
    if (data_len == 0) return;

    // Cannot run more than one command at a time.
    if (phase != PHASE_READY) {
      ble_send_reply(STATUS_BUSY);
      return;
    }

    // See which command should be started.
    if (cmd->any.command == COMMAND_RESET) {
        LOG("command: reset");
        // The reset will happen in the disconnect event.
        phase = PHASE_RESETTING;
        ble_disconnect();
    } else if (cmd->any.command == COMMAND_START) {
        if (data_len < sizeof(cmd->start)) {
            return;
        }
        LOG("command: start");
        if (cmd->start.startAddr != APP_CODE_BASE) {
          ble_send_reply(STATUS_INVALID_ERASE_START);
          return;
        }
        if (cmd->start.startAddr + cmd->start.length > (uint32_t)_stext) {
          // Note: using > instead of >= because if the entire application
          // flash area is filled, the next address (start + length) will be
          // the bootloader.
          ble_send_reply(STATUS_INVALID_ERASE_LENGTH);
          return;
        }
        if (cmd->start.startAddr % 4 != 0) {
          // The app size must be aligned to 4 bytes.
          ble_send_reply(STATUS_INVALID_ERASE_LENGTH);
          return;
        }
        flash_write_app_size = cmd->start.length;
        flash_write_index = 0;
        flash_write_current_page = APP_CODE_BASE / PAGE_SIZE;
        ble_send_reply(STATUS_ERASE_STARTED);

        // Start erasing the flash.
        phase = PHASE_ERASING;
        flash_erase_current_page = cmd->start.startAddr / PAGE_SIZE;
        flash_erase_last_page = (cmd->start.startAddr + cmd->start.length) / PAGE_SIZE;
        resume_flash_erase();
#if DEBUG
    } else if (cmd->any.command == COMMAND_PING) {
        // Only for debugging
        LOG("command: ping");
        ble_send_reply(STATUS_PONG);
#endif
    } else if (cmd->any.command == COMMAND_RESET_BOOTLOADER) {
        LOG("command: reset bootloader");
        // Nothing to do here, we're already in the bootloader.
    } else {
        LOG("command: ???");
    }
}

// handle_data is called when a new value is written by the client to the data
// characteristic.
void handle_data(uint16_t data_len, uint8_t *data) {
    if (phase != PHASE_WRITING) {
        LOG("got data while not in writing state");
        return;
    }
    for (int i=0; i<data_len; i++) {
        if (flash_write_index >= flash_write_app_size) continue;
        flash_write_buf[flash_write_index % (PAGE_SIZE * 2)] = data[i];
        flash_write_index++;
        if (flash_write_index == flash_write_app_size) {
            // Last byte of the app has been received. Start writing this page
            // to flash, even if it isn't a full page.
            LOG("received everything");
            phase = PHASE_WRITING_LAST_PAGE;
            write_current_page();
        } else if (flash_write_index % PAGE_SIZE == 0) {
            // All data in this flash page has been received, so start writing
            // this page to flash.
            LOG("next page");
            write_current_page();
        }
    }
}

// handle_disconnect is called when the client disconnects.
void handle_disconnect(void) {
    if (phase == PHASE_RESETTING) {
        // The client requested a reset, which we do after disconnecting.
        sd_nvic_SystemReset();
        __builtin_unreachable();
    }
}

// sd_evt_handler is called for non-BLE events. In particular, it is called for
// all flash related events.
void sd_evt_handler(uint32_t evt_id) {
    switch (evt_id) {
    case NRF_EVT_FLASH_OPERATION_SUCCESS:
        switch (phase) {
        case PHASE_ERASING:
            LOG("sd evt: flash operation finished");
            if (flash_erase_current_page == flash_erase_last_page) {
              phase = PHASE_WRITING;
              ble_send_reply(STATUS_ERASE_FINISHED);
              return;
            }
            flash_erase_current_page++;
            resume_flash_erase();
            break;
        case PHASE_WRITING:
            // Page was successfully written.
            flash_write_current_page++;
            break;
        case PHASE_WRITING_LAST_PAGE:
            // Everything is finished!
            phase = PHASE_READY;
            ble_send_reply(STATUS_WRITE_FINISHED);
            break;
        default:
            LOG_NUM("NRF_EVT_FLASH_OPERATION_SUCCESS: unknown state", phase);
            break;
        }
        break;
    case NRF_EVT_FLASH_OPERATION_ERROR:
        switch (phase) {
        case PHASE_ERASING:
            LOG("sd evt: erase failed");
            ble_send_reply(STATUS_ERASE_FAILED);
            break;
        case PHASE_WRITING:
        case PHASE_WRITING_LAST_PAGE:
            LOG("sd evt: write failed");
            ble_send_reply(STATUS_WRITE_FAILED);
            break;
        default:
            LOG("sd evt: unknown flash operation");
            break;
        }
        // Reset back to the start, so that a new attempt can be made.
        phase = PHASE_READY;
        break;
    default:
        LOG_NUM("sd evt:", evt_id);
        break;
    }
}

// resume_flash_erase is called either right after a COMMAND_START is received
// or after the previous flash page erase was finished. It will continue to
// erase the next page that should be erased.
static void resume_flash_erase(void) {
    LOG_NUM("erasing:", flash_erase_current_page);
    uint32_t err_code = sd_flash_page_erase(flash_erase_current_page);
    if (err_code != 0) {
        LOG("  error: cannot schedule page erase");
        // Error: the erase command wasn't scheduled.
        ble_send_reply(STATUS_ERASE_FAILED);
    }
    if (err_code == NRF_ERROR_INTERNAL) {
        LOG("! internal error");
    } else if (err_code == NRF_ERROR_BUSY) {
        LOG("! busy");
    } else if (err_code != 0) {
        LOG("! could not start erase of page");
    }
}

// write_current_page writes the last received code page to flash. No write may
// be in progress or an error will be sent to the client.
static void write_current_page(void) {
    // Determine the flash page number, which is useful for later calculations.
    uint32_t page = (flash_write_index - 1 + APP_CODE_BASE) / PAGE_SIZE;
    if (page != flash_write_current_page) {
        // Previous page was not fully written.
        // Maybe the SoftDevice couldn't schedule the page write in time?
        LOG("previous page was not completely written");
        ble_send_reply(STATUS_WRITE_TOO_FAST);
        return;
    }

    // Determine the length of the packet. Modulo would not be entirely correct
    // here, as the length would never reach PAGE_SIZE (being zero at that
    // point). Hence the check for zero.
    uint32_t length = flash_write_index % PAGE_SIZE;
    if (length == 0) {
        length = PAGE_SIZE;
    }

    LOG_NUM("write page:", page);
    LOG_NUM("  length:  ", length);
    uint32_t *p_dst = (uint32_t*)(page * PAGE_SIZE);
    uint32_t *p_src = (uint32_t*)(flash_write_buf + (flash_write_index - 1) / PAGE_SIZE * PAGE_SIZE);
    uint32_t err_code = sd_flash_write(p_dst, p_src, length / 4);
    if (err_code != 0) {
        LOG_NUM("  error: could not start page write", err_code);
        ble_send_reply(STATUS_WRITE_FAILED);
        return;
    }
}
