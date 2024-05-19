/*

let &makeprg = "gcc test.c bootrom/coredump.c"

 */

#include "bootrom/coredump.h"

#include <stdint.h>
#include <stdio.h>

const uint8_t __flash_binary_start;
const uint8_t __flash_binary_end;

int main(void) {
    FILE *elf = fopen("custom.elf", "wb");
    if (elf == NULL)
    {
        fprintf(stderr, "Failed to open file\n");
        return 1;
    }

    fprintf(stderr, "ELF_HDR_SIZE:            %8ld (0x%08X)\n", ELF_HDR_SIZE, ELF_HDR_SIZE);
    fprintf(stderr, "PROGRAM_HDR_OFFSET:      %8ld (0x%08X)\n", PROGRAM_HDR_OFFSET, PROGRAM_HDR_OFFSET);
    fprintf(stderr, "PROGRAM_HDR_ENTRY_SIZE:  %8ld (0x%08X)\n", PROGRAM_HDR_ENTRY_SIZE, PROGRAM_HDR_ENTRY_SIZE);
    fprintf(stderr, "PROGRAM_HDR_ENTRY_COUNT: %8ld (0x%08X)\n", PROGRAM_HDR_ENTRY_COUNT, PROGRAM_HDR_ENTRY_COUNT);
    fprintf(stderr, "PROGRAM_HDR_SIZE:        %8ld (0x%08X)\n", PROGRAM_HDR_SIZE, PROGRAM_HDR_SIZE);
    fprintf(stderr, "PROGRAM_HDR_END:         %8ld (0x%08X)\n", PROGRAM_HDR_END, PROGRAM_HDR_END);
    fprintf(stderr, "SECTION_HDR_OFFSET:      %8ld (0x%08X)\n", SECTION_HDR_OFFSET, SECTION_HDR_OFFSET);
    fprintf(stderr, "SECTION_HDR_ENTRY_SIZE:  %8ld (0x%08X)\n", SECTION_HDR_ENTRY_SIZE, SECTION_HDR_ENTRY_SIZE);
    fprintf(stderr, "SECTION_HDR_ENTRY_COUNT: %8ld (0x%08X)\n", SECTION_HDR_ENTRY_COUNT, SECTION_HDR_ENTRY_COUNT);
    fprintf(stderr, "SECTION_HDR_SIZE:        %8ld (0x%08X)\n", SECTION_HDR_SIZE, SECTION_HDR_SIZE);
    fprintf(stderr, "SECTION_HDR_END:         %8ld (0x%08X)\n", SECTION_HDR_END, SECTION_HDR_END);
    fprintf(stderr, "SECTION_HDR_STR_INDEX:   %8ld (0x%08X)\n", SECTION_HDR_STR_INDEX, SECTION_HDR_STR_INDEX);
    fprintf(stderr, "NOTE_OFFSET:             %8ld (0x%08X)\n", NOTE_OFFSET, NOTE_OFFSET);
    fprintf(stderr, "NOTE_SIZE:               %8ld (0x%08X)\n", NOTE_SIZE, NOTE_SIZE);
    fprintf(stderr, "NOTE_END:                %8ld (0x%08X)\n", NOTE_END, NOTE_END);
    fprintf(stderr, "MEM_OFFSET:              %8ld (0x%08X)\n", MEM_OFFSET, MEM_OFFSET);
    fprintf(stderr, "MEM_SIZE:                %8ld (0x%08X)\n", MEM_SIZE, MEM_SIZE);
    fprintf(stderr, "MEM_END:                 %8ld (0x%08X)\n", MEM_END, MEM_END);
    fprintf(stderr, "FLASH_OFFSET:            %8ld (0x%08X)\n", FLASH_OFFSET, FLASH_OFFSET);
    fprintf(stderr, "FLASH_SIZE:              %8ld (0x%08X)\n", FLASH_SIZE, FLASH_SIZE);
    fprintf(stderr, "FLASH_END:               %8ld (0x%08X)\n", FLASH_END, FLASH_END);
    fprintf(stderr, "FLASH_SYM_BASE:          %8ld (0x%08X)\n", FLASH_SYM_BASE, FLASH_SYM_BASE);
    fprintf(stderr, "FLASH_SYM_END:           %8ld (0x%08X)\n", FLASH_SYM_END, FLASH_SYM_END);
    fprintf(stderr, "STR_SECTION_OFFSET:      %8ld (0x%08X)\n", STR_SECTION_OFFSET, STR_SECTION_OFFSET);
    fprintf(stderr, "STR_NULL_OFFSET:         %8ld (0x%08X)\n", STR_NULL_OFFSET, STR_NULL_OFFSET);
    fprintf(stderr, "STR_0:                   %8ld (0x%08X)\n", STR_0, STR_0);
    fprintf(stderr, "STR_NOTE0_OFFSET:        %8ld (0x%08X)\n", STR_NOTE0_OFFSET, STR_NOTE0_OFFSET);
    fprintf(stderr, "STR_1:                   %8ld (0x%08X)\n", STR_1, STR_1);
    fprintf(stderr, "STR_MEM_OFFSET:          %8ld (0x%08X)\n", STR_MEM_OFFSET, STR_MEM_OFFSET);
    fprintf(stderr, "STR_2:                   %8ld (0x%08X)\n", STR_2, STR_2);
    fprintf(stderr, "STR_FLASH_OFFSET:        %8ld (0x%08X)\n", STR_FLASH_OFFSET, STR_FLASH_OFFSET);
    fprintf(stderr, "STR_3:                   %8ld (0x%08X)\n", STR_3, STR_3);
    fprintf(stderr, "STR_SHSTRTAB_OFFSET:     %8ld (0x%08X)\n", STR_SHSTRTAB_OFFSET, STR_SHSTRTAB_OFFSET);
    fprintf(stderr, "STR_4:                   %8ld (0x%08X)\n", STR_4, STR_4);
    fprintf(stderr, "STR_CONTENTS:            %8ld (0x%08X)\n", STR_CONTENTS, STR_CONTENTS);
    fprintf(stderr, "STR_SIZE:                %8ld (0x%08X)\n", STR_SIZE, STR_SIZE);
    fprintf(stderr, "STR_SECTION_SIZE:        %8ld (0x%08X)\n", STR_SECTION_SIZE, STR_SECTION_SIZE);
    fprintf(stderr, "STR_SECTION_END:         %8ld (0x%08X)\n", STR_SECTION_END, STR_SECTION_END);

    uint8_t buf[12];

    size_t elf_bytes = get_coredump_size();
    for (size_t offset = 0; offset < elf_bytes; offset += sizeof(buf))
    {
        fprintf(stderr, "Progress %d/%d (%d%%)\n",
                (int)offset, (int)elf_bytes, (int)(100 * offset / elf_bytes));
        get_coredump_chunk(buf, offset, sizeof(buf));
        fwrite(buf, sizeof(buf[0]), sizeof(buf)/sizeof(buf[0]), elf);
    }
}
