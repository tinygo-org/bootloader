MEMORY
{
    FLASH_TEXT (rw) : ORIGIN = 1M         - 8K,  LENGTH = 8K
    FLASH_BOOT (r)  : ORIGIN = 0x10001014,       LENGTH = 4  /* 4 bytes, UICR.NRFFW[0] */
    RAM (xrw)       : ORIGIN = 0x20000000 + 16K, LENGTH = 16K
}

INCLUDE "common.ld"
