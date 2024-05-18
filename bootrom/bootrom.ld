MEMORY {
    BOOT2(rx) : ORIGIN = 0x10000000, LENGTH = 0x100
    FLASH(rx) : ORIGIN = 0x10000100, LENGTH = 16K
    SRAM(rwx) : ORIGIN = 0x20000000, LENGTH = 264K
    USBRAM(rw) : ORIGIN = 0x50100400, LENGTH = 3K
}

SECTIONS {
    .text : {
        KEEP(*(.reset))
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

    ASSERT(__crash____irq5_vector == __crash____vectors + 0x40 + 5 * 4, "too much data in middle of vector table")
    ASSERT(SIZEOF(.data) == 0,
        "ERROR: do not use static memory in bootrom! (.data)")
}
