SECTIONS
{
    /* This is stored in the UICR registers at address 0x10001014. */
    .bootloaderaddr :
    {
        KEEP(*(.bootloaderaddr))
    } >FLASH_BOOT

    .text :
    {
        _stext = .;
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
        _etext = .;
    } >FLASH_TEXT

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT>FLASH_TEXT

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >RAM
}

/* top end of the stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);
