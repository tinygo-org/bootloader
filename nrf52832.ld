MEMORY
{
    FLASH_TEXT (rw) : ORIGIN = 512K       - __bootloader_size, LENGTH = __bootloader_size
    FLASH_BOOT (r)  : ORIGIN = 0x10001014,                     LENGTH = 4  /* 4 bytes, UICR.NRFFW[0] */
    RAM (xrw)       : ORIGIN = 0x20000000 + 16K,               LENGTH = 16K
}

__bootloader_size = 4K;

INCLUDE "common.ld"
