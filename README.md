# DFU bootloader

This is a small bootloader/DFU (device firmware updater) for nRF52 chips that can do over-the-air firmware updates. It works at least on the nRF52832 with s132, but other chips/SoftDevices will be easy to add or may already work.

## Installing

Download the code:

    git clone --recursive https://github.com/tinygo-org/bootloader.git

Build and flash:

    make flash CHIP=nrf52832

This will flash the new bootloader to the device. It is recommended to have the SoftDevice and the application already installed. If there was a bootloader installed before, you may need to erase the whole chip to erase the UICR, which contains the bootloader configuration among others.

## Bluetooth API

The DFU advertises a service with two characteristics, one for commands and replies and one for sending bulk data. Commands are sent by writing to the command characteristic and replies are sent back with notifications.

Updating the device firmware follows the following steps:

 1. If needed, reset into DFU mode. This is done by sending the `COMMAND_RESET_BOOTLOADER` command. The bootloader will ignore this command, so it can be safely sent.
 2. Send a `COMMAND_START` with some parameters. The packet starts with the command (`\x02`), followed by three zero bytes for padding, followed by a 4 byte little endian start address, followed by a 4 byte little endian length address.
 3. The bootloader will send a `STATUS_ERASE_STARTED` started back to indicate the command has been accepted.
 4. The flash will be erased. This might take a short while. When the bootloader is finished, it will send a `STATUS_ERASE_FINISHED` back.
 5. The client can now start streaming the application data. It will be written to flash as needed, filling up the space until the firmware length has been reached (as sent in the `COMMAND_START` packet).
 6. Once finished, the bootloader will send a `STATUS_WRITE_FINISHED` back. At this point, the application has been overwritten successfully.
 7. The client can now send a `COMMAND_RESET` so that the bootloader will reset, starting the new application.

For details, see dfuclient/main.go, dfu.h, and dfu.c.

## Optimizations

This bootloader is very small for one that supports DFU over BLE. This is in part thanks to some possibly dangerous optimizations:

  * Code immediately follows the ISR vector with no reserved space.
  * Product anomalies are ignored. If they are relevant to the bootloader, they can be implemented.
  * Memory regions are not enabled by default. With the initial values (after reset) they are already enabled. This should be safe as long as no reset happens while memory regions are disabled.
  * The ISR vector is shortened to 4 words, saving a lot of space. This includes the initial stack pointer and the HardFault handler. Other interrupts should not happen, I hope.

Still, there are some more optimizations that can be done to get to a lower size:

  * SVC functions (all `sd_` calls) are currently implemented in a separate function. It is possible to inline those, or at least some of them. As a `svc` instruction is just 2 bytes and a function call is 4 bytes + the function itself (another 4 bytes), inlined SVC functions are always more efficient. it should be possible to save about 100 bytes this way.
