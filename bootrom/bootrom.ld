MEMORY {
    BOOT2(rx) : ORIGIN = 0x10000000, LENGTH = 0x100
    FLASH(rx) : ORIGIN = 0x10000100, LENGTH = 16K
    SRAM(rwx) : ORIGIN = 0x20000000, LENGTH = 264K
    USBRAM(rw) : ORIGIN = 0x50100400, LENGTH = 3K
}

SECTIONS {
    .boot2 : {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > BOOT2
    ASSERT(__boot2_end__ - __boot2_start__ == 256,
        "ERROR: Pico second stage bootloader must be 256 bytes in size")

    .text : {
        KEEP(*(.vectors))
        *(.text*)
        KEEP(*(.rodata.keep*))
        *(.rodata*)
    } >FLASH

    .data : {
        *(.data*)
    } >USBRAM

    .bss : {
        *(.bss*)
    } >USBRAM

    ASSERT(__irq5_vector == __vectors + 0x40 + 5 * 4, "too much data in middle of vector table")
    ASSERT(SIZEOF(.data) == 0,
        "ERROR: do not use static memory in bootrom! (.data)")

     /* Leave room above the stack for stage 2 load, so that stage 2
       can image SRAM from its beginning */
    _stacktop = ORIGIN(SRAM) + LENGTH(SRAM) - 256;
}
