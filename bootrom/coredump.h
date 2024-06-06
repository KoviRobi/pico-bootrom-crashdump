#pragma once

#include <stdint.h>
#include <stdlib.h>

#define ELF_HDR_SIZE            0x0034
#define PROGRAM_HDR_OFFSET      ELF_HDR_SIZE
#define PROGRAM_HDR_ENTRY_SIZE  0x20
#define PROGRAM_HDR_ENTRY_COUNT 3
#define PROGRAM_HDR_SIZE        (PROGRAM_HDR_ENTRY_SIZE * PROGRAM_HDR_ENTRY_COUNT)
#define PROGRAM_HDR_END         (PROGRAM_HDR_OFFSET + PROGRAM_HDR_SIZE)
#define SECTION_HDR_OFFSET      STR_SECTION_END
#define SECTION_HDR_ENTRY_SIZE  0x28
#define SECTION_HDR_ENTRY_COUNT 0x5
#define SECTION_HDR_SIZE        (SECTION_HDR_ENTRY_SIZE * SECTION_HDR_ENTRY_COUNT)
#define SECTION_HDR_END         (SECTION_HDR_OFFSET + SECTION_HDR_SIZE)
#define SECTION_HDR_STR_INDEX   (SECTION_HDR_ENTRY_COUNT - 1)

#define NOTE_OFFSET  (ELF_HDR_SIZE + PROGRAM_HDR_SIZE)
#define NOTE_SIZE    0x0 // TODO
#define NOTE_END     (NOTE_OFFSET + NOTE_SIZE)
#define MEM_OFFSET   NOTE_END
#define MEM_SIZE     (SRAM_END - SRAM_BASE)
#define MEM_END      (MEM_OFFSET + MEM_SIZE)
#define FLASH_OFFSET MEM_END
#define FLASH_SIZE   (FLASH_SYM_END - FLASH_SYM_BASE) // TODO: Does LTO optimise this?
#define FLASH_END    (FLASH_OFFSET + FLASH_SIZE)

// Linker defined
extern const uint8_t __flash_binary_start;
extern const uint8_t __flash_binary_end;
#define FLASH_SYM_BASE ((uintptr_t)&__flash_binary_start)
#define FLASH_SYM_END ((uintptr_t)&__flash_binary_end)

#define ALIGN_4BYTE(ADDR) ((ADDR + 3) & ~3)

// Note: sizeof("FOO" "BAR") includes the null terminator after "BAR", but
// "FOO" and "BAR" are joined without null terminator
#define STR_SECTION_OFFSET  FLASH_END
#define STR_NULL_OFFSET     0
#define STR_0               ""
#define STR_NOTE0_OFFSET    (sizeof(STR_0))
#define STR_1               STR_0 "\0" ".note.core"
#define STR_MEM_OFFSET      (sizeof(STR_1))
#define STR_2               STR_1 "\0" ".data.sram"
#define STR_FLASH_OFFSET    (sizeof(STR_2))
#define STR_3               STR_2 "\0" ".text.flash"
#define STR_SHSTRTAB_OFFSET (sizeof(STR_3))
#define STR_4               STR_3 "\0" ".shstrtab"
#define STR_CONTENTS        (STR_4)
#define STR_SIZE            (sizeof(STR_CONTENTS))
#define STR_SECTION_SIZE    ALIGN_4BYTE(STR_SIZE)
#define STR_SECTION_END     (STR_SECTION_OFFSET + STR_SECTION_SIZE)

/// Get any bit of the coredump
/// \note Offset must be a multiple of 4 bytes
void get_coredump_chunk(uint8_t * dest, size_t file_offset, size_t buf_size);

/// Gets the size of the coredump in bytes.
size_t get_coredump_size(void);

// TODO
#define SRAM_BASE 0
#define SRAM_END  0x10
#undef FLASH_SYM_BASE
#undef FLASH_SYM_END
#define FLASH_SYM_BASE 0x1234
#define FLASH_SYM_END  0x1244

#define GDB_TDESC_XML                                                                          \
    "<?xml version=\"1.0\"?>"                                                                  \
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"                                              \
    "<target>"                                                                                 \
      "<architecture>arm</architecture>"                                                       \
      "<feature name=\"org.gnu.gdb.arm.m-profile\">"                                           \
        "<reg name=\"r0\" bitsize=\"32\" type=\"int\" regnum=\"0\" group=\"general\"/>"        \
        "<reg name=\"r1\" bitsize=\"32\" type=\"int\" regnum=\"1\" group=\"general\"/>"        \
        "<reg name=\"r2\" bitsize=\"32\" type=\"int\" regnum=\"2\" group=\"general\"/>"        \
        "<reg name=\"r3\" bitsize=\"32\" type=\"int\" regnum=\"3\" group=\"general\"/>"        \
        "<reg name=\"r4\" bitsize=\"32\" type=\"int\" regnum=\"4\" group=\"general\"/>"        \
        "<reg name=\"r5\" bitsize=\"32\" type=\"int\" regnum=\"5\" group=\"general\"/>"        \
        "<reg name=\"r6\" bitsize=\"32\" type=\"int\" regnum=\"6\" group=\"general\"/>"        \
        "<reg name=\"r7\" bitsize=\"32\" type=\"int\" regnum=\"7\" group=\"general\"/>"        \
        "<reg name=\"r8\" bitsize=\"32\" type=\"int\" regnum=\"8\" group=\"general\"/>"        \
        "<reg name=\"r9\" bitsize=\"32\" type=\"int\" regnum=\"9\" group=\"general\"/>"        \
        "<reg name=\"r10\" bitsize=\"32\" type=\"int\" regnum=\"10\" group=\"general\"/>"      \
        "<reg name=\"r11\" bitsize=\"32\" type=\"int\" regnum=\"11\" group=\"general\"/>"      \
        "<reg name=\"r12\" bitsize=\"32\" type=\"int\" regnum=\"12\" group=\"general\"/>"      \
        "<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\" regnum=\"13\" group=\"general\"/>"  \
        "<reg name=\"lr\" bitsize=\"32\" type=\"int\" regnum=\"14\" group=\"general\"/>"       \
        "<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\" regnum=\"15\" group=\"general\"/>"  \
        "<reg name=\"xPSR\" bitsize=\"32\" type=\"int\" regnum=\"16\" group=\"general\"/>"     \
      "</feature>"                                                                             \
      "<feature name=\"org.gnu.gdb.arm.m-system\">"                                            \
        "<reg name=\"msp\" bitsize=\"32\" type=\"data_ptr\" regnum=\"17\" group=\"system\"/>"  \
        "<reg name=\"psp\" bitsize=\"32\" type=\"data_ptr\" regnum=\"18\" group=\"system\"/>"  \
        "<reg name=\"primask\" bitsize=\"1\" type=\"int8\" regnum=\"20\" group=\"system\"/>"   \
        "<reg name=\"basepri\" bitsize=\"8\" type=\"int8\" regnum=\"21\" group=\"system\"/>"   \
        "<reg name=\"faultmask\" bitsize=\"1\" type=\"int8\" regnum=\"22\" group=\"system\"/>" \
        "<reg name=\"control\" bitsize=\"3\" type=\"int8\" regnum=\"23\" group=\"system\"/>"   \
      "</feature>"                                                                             \
    "</target>"

#define NT_GDB_TDESC 0xff000000 /* Contains copy of GDB's target description XML. */

#define PRSTATUS  NT_PRSTATUS,  (uint8_t *)"CORE", sizeof("CORE")
#define PRSTATUS  NT_PRSTATUS,  (uint8_t *)"CORE", sizeof("CORE")
#define GDB_TDESC NT_GDB_TDESC, (uint8_t *)"GDB", sizeof("GDB")
