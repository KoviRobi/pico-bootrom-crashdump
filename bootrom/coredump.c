// #include "hardware/regs/addressmap.h"

#include "coredump.h"

// TODO
#include <elf.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// TODO:
#include <stdio.h>

// \note end points to the first free byte
static void put_u8(uint8_t **const buf, const uint8_t *const end, const uint8_t value) {
    if (*buf < end) *(*buf)++ = value;
}

// \note end points to the first free byte
static void put_u16el(uint8_t **const buf, const uint8_t *const end, const uint16_t value) {
    put_u8(buf, end, (value >> 0) & 0xFF);
    put_u8(buf, end, (value >> 8) & 0xFF);
}

// \note end points to the first free byte
static void put_u32el(uint8_t **const buf, const uint8_t *const end, const uint32_t value) {
    put_u8(buf, end, (value >>  0) & 0xFF);
    put_u8(buf, end, (value >>  8) & 0xFF);
    put_u8(buf, end, (value >> 16) & 0xFF);
    put_u8(buf, end, (value >> 24) & 0xFF);
}

// \note end points to the first free byte
static void put_bytes(uint8_t **const buf, const uint8_t *end, const uint8_t *src, size_t size) {
    for (size_t idx = 0; idx < size; ++idx) {
        put_u8(buf, end, src[idx]);
    }
}

/// Writes 0x34/52 bytes of ELF32 headers
/// \note Offset must be a multiple of 4 bytes
/// \see ELF_HDR_SIZE
static void put_elf_header(uint8_t **const buf, const uint8_t *const end, const size_t offset) {
    printf("%s(%p, %p, %lu)\n", __func__, *buf, end, offset); // TODO
    assert((offset & 0x3) == 0);
    switch (offset) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    default:   assert(false); break;
    case 0x00: put_bytes(buf, end, (uint8_t*)ELFMAG, SELFMAG);
    case 0x04: put_u8(buf, end, ELFCLASS32);
    case 0x05: put_u8(buf, end, ELFDATA2LSB);
    case 0x06: put_u8(buf, end, EV_CURRENT);
    case 0x07: put_u8(buf, end, ELFOSABI_ARM);
    case 0x08: put_u32el(buf, end, 0); // Padding
    case 0x0C: put_u32el(buf, end, 0); // Padding
    case 0x10: put_u16el(buf, end, ET_CORE);
    case 0x12: put_u16el(buf, end, EM_ARM);
    case 0x14: put_u32el(buf, end, EV_CURRENT);
    case 0x18: put_u32el(buf, end, 0); // Entry point, irrelevant for core
    case 0x1C: put_u32el(buf, end, PROGRAM_HDR_OFFSET);
    case 0x20: put_u32el(buf, end, SECTION_HDR_OFFSET);
    case 0x24: put_u32el(buf, end, 0); // Flags
    case 0x28: put_u16el(buf, end, ELF_HDR_SIZE);
    case 0x2A: put_u16el(buf, end, PROGRAM_HDR_ENTRY_SIZE);
    case 0x2C: put_u16el(buf, end, PROGRAM_HDR_ENTRY_COUNT);
    case 0x2E: put_u16el(buf, end, SECTION_HDR_ENTRY_SIZE);
    case 0x30: put_u16el(buf, end, SECTION_HDR_ENTRY_COUNT);
    case 0x32: put_u16el(buf, end, SECTION_HDR_STR_INDEX);
#pragma GCC diagnostic pop
    }
}

/// Put program headers
/// \note Offset must be a multiple of 4 bytes
/// \see PROGRAM_HDR_ENTRY_SIZE, PROGRAM_HDR_ENTRY_COUNT
static void put_program_headers(uint8_t **const buf, const uint8_t *const end, const size_t offset) {
    printf("%s(%p, %p, %lu)\n", __func__, *buf, end, offset); // TODO
    assert((offset & 0x3) == 0);
    const size_t note = 0x0;
    const size_t ram = PROGRAM_HDR_ENTRY_SIZE;
    const size_t flash = 2 * PROGRAM_HDR_ENTRY_SIZE;
    switch (offset) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    case note+0x00: put_u32el(buf, end, PT_LOAD);
    case note+0x04: put_u32el(buf, end, NOTE_OFFSET);
    case note+0x08: put_u32el(buf, end, 0); // Virtual addr
    case note+0x0C: put_u32el(buf, end, 0); // Physical addr
    case note+0x10: put_u32el(buf, end, NOTE_SIZE); // File size
    case note+0x14: put_u32el(buf, end, 0); // Mem size
    case note+0x18: put_u32el(buf, end, PF_R); // Flags
    case note+0x1C: put_u32el(buf, end, 1); // Align

    case ram+0x00: put_u32el(buf, end, PT_LOAD);
    case ram+0x04: put_u32el(buf, end, MEM_OFFSET);
    case ram+0x08: put_u32el(buf, end, SRAM_BASE); // Virtual addr
    case ram+0x0C: put_u32el(buf, end, SRAM_BASE); // Physical addr
    case ram+0x10: put_u32el(buf, end, MEM_SIZE); // File size
    case ram+0x14: put_u32el(buf, end, MEM_SIZE); // Mem size
    case ram+0x18: put_u32el(buf, end, PF_R | PF_W); // Flags
    case ram+0x1C: put_u32el(buf, end, 1); // Align

    case flash+0x00: put_u32el(buf, end, PT_LOAD);
    case flash+0x04: put_u32el(buf, end, FLASH_OFFSET);
    case flash+0x08: put_u32el(buf, end, FLASH_SYM_BASE); // Virtual addr
    case flash+0x0C: put_u32el(buf, end, FLASH_SYM_BASE); // Physical addr
    case flash+0x10: put_u32el(buf, end, FLASH_SIZE); // File size
    case flash+0x14: put_u32el(buf, end, FLASH_SIZE); // Mem size
    case flash+0x18: put_u32el(buf, end, PF_R | PF_X); // Flags
    case flash+0x1C: put_u32el(buf, end, 1); // Align
#pragma GCC diagnostic pop
    }
}

static void put_note_for(
        uint8_t **buf, const uint8_t *const end, const size_t offset,
        const uint32_t type, const uint8_t *name, const size_t name_len,
        const uint8_t *data, const size_t data_len) {
    assert(offset == 0); // TODO
    put_u32el(buf, end, name_len);
}

static void put_note(uint8_t **const buf, const uint8_t *const end, const size_t offset) {
    printf("%s(%p, %p, %lu)\n", __func__, *buf, end, offset); // TODO
    assert(offset == 0); // TODO
    uint32_t core0_regs[] = {
    };
    uint32_t core1_regs[]  = {
    };
    put_note_for(buf, end, offset, PRSTATUS, (uint8_t *)core0_regs, sizeof(core0_regs));
    put_note_for(buf, end, offset, PRSTATUS, (uint8_t *)core1_regs, sizeof(core1_regs));
    put_note_for(buf, end, offset, GDB_TDESC, (uint8_t *)GDB_TDESC_XML, sizeof(GDB_TDESC_XML));
}

static void put_mem(uint8_t **const buf, const uint8_t *const end, const size_t offset) {
    printf("%s(%p, %p, %lu)\n", __func__, *buf, end, offset); // TODO
    for (size_t idx = offset; idx < MEM_SIZE - offset; ++idx)
    {
        put_u8(buf, end, (uint8_t)idx); // TODO
    }
}

static void put_flash(uint8_t **const buf, const uint8_t *const end, const size_t offset) {
    printf("%s(%p, %p, %lu)\n", __func__, *buf, end, offset); // TODO
    for (size_t idx = offset; idx < FLASH_SIZE - offset; ++idx)
    {
        put_u8(buf, end, (uint8_t)idx); // TODO
    }
}

static void put_str_section(uint8_t **const buf, const uint8_t *const end, const size_t offset) {
    static const char * str_section_data = STR_CONTENTS;
    for (size_t idx = offset; idx < STR_SECTION_SIZE; ++idx)
    {
        if (idx < STR_SIZE)
        {
            put_u8(buf, end, str_section_data[idx]);
        }
        else
        {
            put_u8(buf, end, 0);
        }
    }
}

/// Put section headers
/// \note Offset must be a multiple of 4 bytes
/// \see SECTION_HDR_ENTRY_SIZE, SECTION_HDR_ENTRY_COUNT
static void put_section_headers(uint8_t **const buf, const uint8_t *const end, const size_t offset) {
    printf("%s(%p, %p, %lu)\n", __func__, *buf, end, offset); // TODO
    assert((offset & 0x3) == 0);
    const size_t null = 0x0;
    const size_t note = SECTION_HDR_ENTRY_SIZE;
    const size_t ram = 2 * SECTION_HDR_ENTRY_SIZE;
    const size_t flash = 3 * SECTION_HDR_ENTRY_SIZE;
    const size_t shstrtab = 4 * SECTION_HDR_ENTRY_SIZE;
    switch (offset) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    case null+0x00: put_u32el(buf, end, STR_NULL_OFFSET);
    case null+0x04: put_u32el(buf, end, SHT_NULL); // Type
    case null+0x08: put_u32el(buf, end, 0); // Flags
    case null+0x0C: put_u32el(buf, end, 0); // Addr
    case null+0x10: put_u32el(buf, end, 0); // File offset
    case null+0x14: put_u32el(buf, end, 0); // Size
    case null+0x18: put_u32el(buf, end, 0); // Link
    case null+0x1C: put_u32el(buf, end, 0); // Info
    case null+0x20: put_u32el(buf, end, 0); // Address align
    case null+0x24: put_u32el(buf, end, 0); // Entsize

    case note+0x00: put_u32el(buf, end, STR_NOTE0_OFFSET);
    case note+0x04: put_u32el(buf, end, SHT_NOTE); // Type
    case note+0x08: put_u32el(buf, end, SHF_ALLOC); // Flags
    case note+0x0C: put_u32el(buf, end, 0); // Addr
    case note+0x10: put_u32el(buf, end, NOTE_OFFSET); // File offset
    case note+0x14: put_u32el(buf, end, NOTE_SIZE); // Size
    case note+0x18: put_u32el(buf, end, 0); // Link
    case note+0x1C: put_u32el(buf, end, 0); // Info
    case note+0x20: put_u32el(buf, end, 1); // Address align
    case note+0x24: put_u32el(buf, end, 0); // Entsize

    case ram+0x00: put_u32el(buf, end, STR_MEM_OFFSET);
    case ram+0x04: put_u32el(buf, end, SHT_PROGBITS); // Type
    case ram+0x08: put_u32el(buf, end, SHF_ALLOC | SHF_WRITE); // Flags
    case ram+0x0C: put_u32el(buf, end, 0); // Addr
    case ram+0x10: put_u32el(buf, end, MEM_OFFSET);
    case ram+0x14: put_u32el(buf, end, MEM_SIZE);
    case ram+0x18: put_u32el(buf, end, 0); // Link
    case ram+0x1C: put_u32el(buf, end, 0); // Info
    case ram+0x20: put_u32el(buf, end, 1); // Address align
    case ram+0x24: put_u32el(buf, end, 0); // Entsize

    case flash+0x00: put_u32el(buf, end, STR_FLASH_OFFSET);
    case flash+0x04: put_u32el(buf, end, SHT_PROGBITS); // Type
    case flash+0x08: put_u32el(buf, end, SHF_ALLOC | SHF_EXECINSTR); // Flags
    case flash+0x0C: put_u32el(buf, end, 0); // Addr
    case flash+0x10: put_u32el(buf, end, FLASH_OFFSET);
    case flash+0x14: put_u32el(buf, end, FLASH_SIZE);
    case flash+0x18: put_u32el(buf, end, 0); // Link
    case flash+0x1C: put_u32el(buf, end, 0); // Info
    case flash+0x20: put_u32el(buf, end, 1); // Address align
    case flash+0x24: put_u32el(buf, end, 0); // Entsize

    case shstrtab+0x00: put_u32el(buf, end, STR_SHSTRTAB_OFFSET);
    case shstrtab+0x04: put_u32el(buf, end, SHT_STRTAB); // Type
    case shstrtab+0x08: put_u32el(buf, end, 0); // Flags
    case shstrtab+0x0C: put_u32el(buf, end, 0); // Addr
    case shstrtab+0x10: put_u32el(buf, end, STR_SECTION_OFFSET);
    case shstrtab+0x14: put_u32el(buf, end, STR_SECTION_SIZE);
    case shstrtab+0x18: put_u32el(buf, end, 0); // Link
    case shstrtab+0x1C: put_u32el(buf, end, 0); // Info
    case shstrtab+0x20: put_u32el(buf, end, 1); // Address align
    case shstrtab+0x24: put_u32el(buf, end, 0); // Entsize
#pragma GCC diagnostic pop
    }
}

void get_coredump_chunk(uint8_t *dest, size_t file_offset, size_t buf_size)
{
    printf("%4d: %s(%p, %lu, %lu)\n", __LINE__, __func__, dest, file_offset, buf_size); // TODO
    assert((file_offset & 0x3) == 0);
    const uint8_t * end = dest + buf_size;

    if (dest < end && file_offset < ELF_HDR_SIZE)
    {
        put_elf_header(&dest, end, file_offset);
        file_offset = ELF_HDR_SIZE;
    }

    printf("%4d: %s(%p, %lu, %lu)\n", __LINE__, __func__, dest, file_offset, buf_size); // TODO
    if (dest < end && file_offset < PROGRAM_HDR_END)
    {
        put_program_headers(&dest, end, file_offset - PROGRAM_HDR_OFFSET);
        file_offset = PROGRAM_HDR_END;
    }

    printf("%4d: %s(%p, %lu, %lu)\n", __LINE__, __func__, dest, file_offset, buf_size); // TODO
    if (dest < end && file_offset < NOTE_END)
    {
        put_note(&dest, end, file_offset - NOTE_OFFSET);
        file_offset = NOTE_END;
    }

    printf("%4d: %s(%p, %lu, %lu)\n", __LINE__, __func__, dest, file_offset, buf_size); // TODO
    if (dest < end && file_offset < MEM_END)
    {
        put_mem(&dest, end, file_offset - MEM_OFFSET);
        file_offset = MEM_END;
    }

    printf("%4d: %s(%p, %lu, %lu)\n", __LINE__, __func__, dest, file_offset, buf_size); // TODO
    if (dest < end && file_offset < FLASH_END)
    {
        put_flash(&dest, end, file_offset - FLASH_OFFSET);
        file_offset = FLASH_END;
    }

    if (dest < end && file_offset < STR_SECTION_END)
    {
      put_str_section(&dest, end, file_offset - STR_SECTION_OFFSET);
      file_offset = STR_SECTION_END;
    }

    if (dest < end && file_offset < SECTION_HDR_END)
    {
      put_section_headers(&dest, end, file_offset - SECTION_HDR_OFFSET);
      file_offset = SECTION_HDR_END;
    }
}

size_t get_coredump_size(void)
{
    return SECTION_HDR_END;
}
