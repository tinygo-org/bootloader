# Some defaults. Run `make clean` when changing these.
CHIP = nrf52840
DEBUG ?= 0

all: build/nrf52840/bootloader.hex

clean:
	@rm -f build/*/*.o build/*/*.elf

flash: build/$(CHIP)/bootloader.hex
	@nrfjprog -f nrf52 --program $< --sectorerase
	@nrfjprog -f nrf52 --reset

# Basic configuration
CC = arm-none-eabi-gcc
CFLAGS += -Os -g -Wall -Werror -mthumb -mcpu=cortex-m4 -flto
LDFLAGS += -Wl,--gc-sections -nostartfiles -flto

CFLAGS += -Ilib/CMSIS/CMSIS/Include
CFLAGS += -Ilib/nrfx
CFLAGS += -Ilib/nrfx/hal
CFLAGS += -Ilib/nrfx/mdk
CFLAGS += -DDEBUG=$(DEBUG)

CFLAGS_NRF52832 += $(CFLAGS)
CFLAGS_NRF52832 += -Ilib/bluetooth/s132_nrf52_6.1.1/s132_nrf52_6.1.1_API/include
CFLAGS_NRF52832 += -Ilib/bluetooth/s132_nrf52_6.1.1/s132_nrf52_6.1.1_API/include/nrf52
CFLAGS_NRF52832 += -DNRF52832_XXAA=1
CFLAGS_NRF52832 += -DNRF52XXX=1
CFLAGS_NRF52832 += -DPCA10040=1

CFLAGS_NRF52840 += $(CFLAGS)
CFLAGS_NRF52840 += -Ilib/bluetooth/s140_nrf52_7.0.1/s140_nrf52_7.0.1_API/include
CFLAGS_NRF52840 += -Ilib/bluetooth/s140_nrf52_7.0.1/s140_nrf52_7.0.1_API/include/nrf52
CFLAGS_NRF52840 += -DNRF52840_XXAA=1
CFLAGS_NRF52840 += -DNRF52XXX=1
CFLAGS_NRF52840 += -DPCA10056=1

build/%/bootloader.hex: build/%/bootloader.elf
	@arm-none-eabi-objcopy -O ihex $< $@

build/nrf52832/bootloader.elf: startup.c main.c ble.c uart.c
	@echo LD $@
	@mkdir -p build/nrf52832
	@$(CC) $(CFLAGS_NRF52832) $(LDFLAGS) -Wl,-T nrf52832.ld -o $@ $^
	@arm-none-eabi-size $@

build/nrf52840/bootloader.elf: startup.c main.c ble.c uart.c
	@echo LD $@
	@mkdir -p build/nrf52840
	@$(CC) $(CFLAGS_NRF52840) $(LDFLAGS) -Wl,-T nrf52840.ld -o $@ $^
	@arm-none-eabi-size $@
