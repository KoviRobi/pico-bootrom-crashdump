/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "runtime.h"
#include "usb_boot_device.h"
#include "virtual_disk.h"
#include "boot/uf2.h"
#include "scsi.h"
#include "usb_msc.h"
#include "async_task.h"
#include "generated.h"

#include "pico/bootrom.h"

// Fri, 05 Sep 2008 16:20:51
#define RASPBERRY_PI_TIME_FRAC 100
#define RASPBERRY_PI_TIME ((16u << 11u) | (20u << 5u) | (51u >> 1u))
#define RASPBERRY_PI_DATE ((28u << 9u) | (9u << 5u) | (5u))
//#define NO_PARTITION_TABLE

#define CLUSTER_SIZE (4096u * CLUSTER_UP_MUL)
#define CLUSTER_SHIFT (3u + CLUSTER_UP_SHIFT)
static_assert(CLUSTER_SIZE == SECTOR_SIZE << CLUSTER_SHIFT, "");

#define CLUSTER_COUNT (VOLUME_SIZE / CLUSTER_SIZE)

#define FIRST_CLUSTER 2
#define CLUS_INDEX 2
#ifdef USE_INFO_UF2
#define CLUS_INFO  3
#define CLUS_CRASH_START 4
#else
#define CLUS_CRASH_START 3
#endif

// See format below for "xxd" function
#define XXD_CHARS_PER_BYTE 4
#define BYTES_DUMPED_PER_SECTOR  (SECTOR_SIZE / XXD_CHARS_PER_BYTE)
#define BYTES_DUMPED_PER_CLUSTER (CLUSTER_SIZE / XXD_CHARS_PER_BYTE)
#define MEM_SIZE (SRAM_END - SRAM_BASE)
#define CLUS_CRASH_LAST (CLUS_CRASH_START + (MEM_SIZE / BYTES_DUMPED_PER_CLUSTER))
#define CRASH_LEN (XXD_CHARS_PER_BYTE * MEM_SIZE)

static_assert(CLUSTER_COUNT <= 65526, "FAT16 limit");

#ifdef NO_PARTITION_TABLE
#define VOLUME_SECTOR_COUNT SECTOR_COUNT
#else
#define VOLUME_SECTOR_COUNT (SECTOR_COUNT-1)
#endif

#define FAT_COUNT 2u
#define MAX_ROOT_DIRECTORY_ENTRIES 512
#define ROOT_DIRECTORY_SECTORS (MAX_ROOT_DIRECTORY_ENTRIES * 32u / SECTOR_SIZE)

#define lsb_hword(x) (((uint)(x)) & 0xffu), ((((uint)(x))>>8u)&0xffu)
#define lsb_word(x) (((uint)(x)) & 0xffu), ((((uint)(x))>>8u)&0xffu),  ((((uint)(x))>>16u)&0xffu),  ((((uint)(x))>>24u)&0xffu)

#define SECTORS_PER_FAT (2 * (CLUSTER_COUNT + SECTOR_SIZE - 1) / SECTOR_SIZE)
static_assert(SECTORS_PER_FAT < 65536, "");

static_assert(VOLUME_SIZE >= 16 * 1024 * 1024, "volume too small for fat16");

// we are a hard drive - SCSI inquiry defines removability
#define IS_REMOVABLE_MEDIA false
#define MEDIA_TYPE (IS_REMOVABLE_MEDIA ? 0xf0u : 0xf8u)

#define MAX_RAM_UF2_BLOCKS 1280
static_assert(MAX_RAM_UF2_BLOCKS >= ((SRAM_END - SRAM_BASE) + (XIP_SRAM_END - XIP_SRAM_BASE)) / 256, "");

static __attribute__((aligned(4))) uint32_t uf2_valid_ram_blocks[(MAX_RAM_UF2_BLOCKS + 31) / 32];

enum partition_type {
    PT_FAT12 = 1,
    PT_FAT16 = 4,
    PT_FAT16_LBA = 0xe,
};

static const uint8_t boot_sector[] = {
        // 00 here should mean not bootable (according to spec) -- still windows unhappy without it
        0xeb, 0x3c, 0x90,
        // 03 id
        'M', 'S', 'W', 'I', 'N', '4', '.', '1',
//        'U', 'F', '2', ' ', 'U', 'F', '2', ' ',
        // 0b bytes per sector
        lsb_hword(512),
        // 0d sectors per cluster
        (CLUSTER_SIZE / SECTOR_SIZE),
        // 0e reserved sectors
        lsb_hword(1),
        // 10 fat count
        FAT_COUNT,
        // 11 max number root entries
        lsb_hword(MAX_ROOT_DIRECTORY_ENTRIES),
        // 13 number of sectors, if < 32768
#if VOLUME_SECTOR_COUNT < 65536
        lsb_hword(VOLUME_SECTOR_COUNT),
#else
        lsb_hword(0),
#endif
        // 15 media descriptor
        MEDIA_TYPE,
        // 16 sectors per FAT
        lsb_hword(SECTORS_PER_FAT),
        // 18 sectors per track (non LBA)
        lsb_hword(1),
        // 1a heads (non LBA)
        lsb_hword(1),
        // 1c hidden sectors 1 for MBR
        lsb_word(SECTOR_COUNT - VOLUME_SECTOR_COUNT),
// 20 sectors if >32K
#if VOLUME_SECTOR_COUNT >= 65536
        lsb_word(VOLUME_SECTOR_COUNT),
#else
        lsb_word(0),
#endif
        // 24 drive number
        0,
        // 25 reserved (seems to be chkdsk flag for clean unmount - linux writes 1)
        0,
        // 26 extended boot sig
        0x29,
        // 27 serial number
        0, 0, 0, 0,
        // 2b label
        'R', 'P', 'I', '-', 'R', 'P', '2', ' ', ' ', ' ', ' ',
        'F', 'A', 'T', '1', '6', ' ', ' ', ' ',
        0xeb, 0xfe // while(1);
};
static_assert(sizeof(boot_sector) == 0x40, "");

#define BOOT_OFFSET_SERIAL_NUMBER 0x27
#define BOOT_OFFSET_LABEL 0x2b

#define ATTR_READONLY       0x01u
#define ATTR_HIDDEN         0x02u
#define ATTR_SYSTEM         0x04u
#define ATTR_VOLUME_LABEL   0x08u
#define ATTR_DIR            0x10u
#define ATTR_ARCHIVE        0x20u

#define MBR_OFFSET_SERIAL_NUMBER 0x1b8

struct dir_entry {
    uint8_t name[11];
    uint8_t attr;
    uint8_t reserved;
    uint8_t creation_time_frac;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t cluster_hi;
    uint16_t last_modified_time;
    uint16_t last_modified_date;
    uint16_t cluster_lo;
    uint32_t size;
};

static_assert(sizeof(struct dir_entry) == 32, "");

static struct uf2_info {
    uint32_t *valid_blocks;
    uint32_t max_valid_blocks;
    uint32_t *cleared_pages;
    uint32_t max_cleared_pages;
    uint32_t num_blocks;
    uint32_t token;
    uint32_t valid_block_count;
    uint32_t lowest_addr;
    uint32_t block_no;
    struct async_task next_task;
    bool ram;
} _uf2_info;

// --- start non IRQ code ---

static void _write_uf2_page_complete(struct async_task *task) {
    if (task->token == _uf2_info.token) {
        if (!task->result && _uf2_info.valid_block_count == _uf2_info.num_blocks) {
            safe_reboot(_uf2_info.ram ? _uf2_info.lowest_addr : 0, SRAM_END, 1000); //300); // reboot in 300 ms
        }
    }
    vd_async_complete(task->token, task->result);
}

// return true for async
static bool _write_uf2_page() {
    reset_usb_boot(usb_activity_gpio_pin_mask, 0);
}

void vd_init() {
}

void vd_reset() {
    usb_debug("Resetting virtual disk\n");
    _uf2_info.num_blocks = 0; // marker that uf2_info is invalid
}

void hex(uint8_t *buf, const uint32_t word, const uint8_t nibbles) {
    for (uint8_t hexit = (8 - nibbles); hexit < 8; ++hexit) {
        const uint8_t offset = hexit - (8 - nibbles);
        const uint8_t nibble = (word >> ((7 - hexit) * 4)) & 0xF;
        if (nibble < 10) {
            buf[offset] = '0' + nibble;
        } else {
            buf[offset] = 'a' + nibble - 10;
        }
    }
}

/// Format is
///
///      addr   0 1  2 3  4 5  6 7  8 9  a b  c d  e f     ascii dump   newline
///      24000 0000 0000 0000 0000 0000 0000 0000 0000  ................\n
///      ^------------------------ 64 characters -----------------------^
///
/// which adds up to 4 characters per byte (64 characters for 16 bytes).
///
/// To  make it easier, we hexdump SECTOR_SIZE==512 *output characters* at a
/// time -- this is exactly 8 lines so we don't have to worry about partial
/// lines.
void xxd(uint8_t *const buf, const uint8_t *const mem) {
    for (uint8_t line = 0; line < 8; ++line) {
        const size_t buf_offset = 64 * line;
        const size_t mem_offset = 16 * line;
        hex(&buf[buf_offset + 0x00], (uintptr_t)&mem[mem_offset], 5);
        buf[buf_offset + 0x05] = ' ';
        for (uint8_t hword = 0; hword < 8; ++hword) {
            const size_t buf_offset_h = buf_offset + 5 * hword;
            const size_t mem_offset_h = mem_offset + 2 * hword;
            hex(&buf[buf_offset_h + 0x06], mem[mem_offset_h + 0], 2);
            hex(&buf[buf_offset_h + 0x08], mem[mem_offset_h + 1], 2);
            buf[buf_offset_h + 0x0a] = ' ';
        } // 5 chars per hword, so up to 0x05 + 5*8 = 45 = 0x2d
        buf[buf_offset + 0x2e] = ' ';
        for (uint8_t b = 0; b < 16; ++b) {
            const size_t buf_offset_b = buf_offset + b;
            uint8_t c = mem[mem_offset + b];
            if (' ' <= c && c <= '~') {
                buf[buf_offset_b + 0x2f] = c;
            } else {
                buf[buf_offset_b + 0x2f] = '.';
            }
        }
        buf[buf_offset + 0x3f] = '\n';
    }
}

// note caller must pass SECTOR_SIZE buffer
void init_dir_entry(struct dir_entry *entry, const char *fn, uint cluster, uint len) {
    entry->creation_time_frac = RASPBERRY_PI_TIME_FRAC;
    entry->creation_time = RASPBERRY_PI_TIME;
    entry->creation_date = RASPBERRY_PI_DATE;
    entry->last_modified_time = RASPBERRY_PI_TIME;
    entry->last_modified_date = RASPBERRY_PI_DATE;
    memcpy(entry->name, fn, 11);
    entry->attr = ATTR_READONLY | ATTR_ARCHIVE;
    entry->cluster_lo = cluster;
    entry->size = len;
}

bool vd_read_block(__unused uint32_t token, uint32_t lba, uint8_t *buf __comma_removed_for_space(uint32_t buf_size)) {
    assert(buf_size >= SECTOR_SIZE);
    memset0(buf, SECTOR_SIZE);
#ifndef NO_PARTITION_TABLE
    if (!lba) {
        uint8_t *ptable = buf + SECTOR_SIZE - 2 - 64;

#if 0
        // simple LBA partition at sector 1
        ptable[4] = PT_FAT16_LBA;
        // 08 LSB start sector
        ptable[8] = 1;
        // 12 LSB sector count
        ptable[12] = (SECTOR_COUNT-1) & 0xffu;
        ptable[13] = ((SECTOR_COUNT-1)>>8u) & 0xffu;
        ptable[14] = ((SECTOR_COUNT-1)>>16u) & 0xffu;
        static_assert(!(SECTOR_COUNT>>24u), "");
#else
        static_assert(!((SECTOR_COUNT - 1u) >> 24), "");
        static const uint8_t _ptable_data4[] = {
                PT_FAT16_LBA, 0, 0, 0,
                lsb_word(1), // sector 1
                // sector count, but we know the MS byte is zero
                (SECTOR_COUNT - 1u) & 0xffu,
                ((SECTOR_COUNT - 1u) >> 8u) & 0xffu,
                ((SECTOR_COUNT - 1u) >> 16u) & 0xffu,
        };
        memcpy(ptable + 4, _ptable_data4, sizeof(_ptable_data4));
#endif
        ptable[64] = 0x55;
        ptable[65] = 0xaa;

        uint32_t sn = msc_get_serial_number32();
        memcpy(buf + MBR_OFFSET_SERIAL_NUMBER, &sn, 4);
        return false;
    }
    lba--;
#endif
    if (!lba) {
        uint32_t sn = msc_get_serial_number32();
        memcpy(buf, boot_sector, sizeof(boot_sector));
        memcpy(buf + BOOT_OFFSET_SERIAL_NUMBER, &sn, 4);
    } else {
        lba--;
        if (lba < SECTORS_PER_FAT * FAT_COUNT) { // FAT region
            // mirror
            while (lba >= SECTORS_PER_FAT) lba -= SECTORS_PER_FAT;
            size_t index = 0;
            uint16_t *p = (uint16_t *) buf;
            if (!lba) {
                p[index++] = 0xff00u | MEDIA_TYPE;
                p[index++] = 0xffff;
                p[index++] = 0xffff; // index.htm
#ifdef USE_INFO_UF2
                p[index++] = 0xffff;  // info_uf2.txt
#endif
            }

            const size_t min_index = lba * (SECTOR_SIZE / 2);
            const size_t max_index = min_index + SECTOR_SIZE / 2 - 1;
            for (size_t clus_crash = MAX(min_index, CLUS_CRASH_START);
                    clus_crash <= MIN(max_index, CLUS_CRASH_LAST);
                    ++clus_crash) {
                // crashdump.xxd -- point to next cluster
                p[index++] = clus_crash + 1;
            }
            // crashdmp.xxd -- patch up last cluster
            if (index-1 == CLUS_CRASH_LAST) {
              p[index-1] = 0xffff;
            }

        } else {
            lba -= SECTORS_PER_FAT * FAT_COUNT;
            if (lba < ROOT_DIRECTORY_SECTORS) {
                // we don't support that many directory entries actually
                if (!lba) {
                    // root directory
                    struct dir_entry *entries = (struct dir_entry *) buf;
                    memcpy(entries[0].name, (boot_sector + BOOT_OFFSET_LABEL), 11);
                    entries[0].attr = ATTR_VOLUME_LABEL | ATTR_ARCHIVE;
                    init_dir_entry(++entries, "INDEX   HTM", CLUS_INDEX, welcome_html_len);
#ifdef USE_INFO_UF2
                    init_dir_entry(++entries, "INFO_UF2TXT", CLUS_INFO, info_uf2_txt_len);
#endif
                    init_dir_entry(++entries, "CRASHDMPXXD", CLUS_CRASH_START, CRASH_LEN);
                }
            } else {
                lba -= ROOT_DIRECTORY_SECTORS;
                uint cluster = (lba >> CLUSTER_SHIFT) + FIRST_CLUSTER;
                uint cluster_offset = lba & ((1u << CLUSTER_SHIFT) - 1);
                if (!cluster_offset) {
                    if (cluster == CLUS_INDEX) {
#ifndef COMPRESS_TEXT
                        memcpy(buf, welcome_html, welcome_html_len);
#else
                        poor_mans_text_decompress(welcome_html_z + sizeof(welcome_html_z), sizeof(welcome_html_z), buf);
                        memcpy(buf + welcome_html_version_offset_1, serial_number_string, 12);
                        memcpy(buf + welcome_html_version_offset_2, serial_number_string, 12);
#endif
                    }
#ifdef USE_INFO_UF2
                    else if (cluster == CLUS_INFO) {
                        // spec suggests we have this as raw text in the binary, although it doesn't much matter if no CURRENT.UF2 file
                        // note that this text doesn't compress anyway, so do this raw anyway
                        memcpy(buf, info_uf2_txt, info_uf2_txt_len);
                    }
#endif
                }

                // This can be in cluster offset 0 (!cluster_offset) and
                // higher, hence not in the above "if" conditional
                if (CLUS_CRASH_START <= cluster && cluster <= CLUS_CRASH_LAST) {
                    xxd(buf,
                            (uint8_t *)SRAM_BASE
                            + (cluster - CLUS_CRASH_START) * BYTES_DUMPED_PER_CLUSTER
                            + cluster_offset * BYTES_DUMPED_PER_SECTOR);
                }
            }
        }
    }
    return false;
}

#define FLASH_MAX_VALID_BLOCKS ((FLASH_BITMAPS_SIZE * 8LL * FLASH_SECTOR_ERASE_SIZE / (FLASH_PAGE_SIZE + FLASH_SECTOR_ERASE_SIZE)) & ~31u)
#define FLASH_CLEARED_PAGES_BASE (FLASH_VALID_BLOCKS_BASE + FLASH_MAX_VALID_BLOCKS / 8)
static_assert(!(FLASH_CLEARED_PAGES_BASE & 0x3), "");
#define FLASH_MAX_CLEARED_PAGES (FLASH_MAX_VALID_BLOCKS * FLASH_PAGE_SIZE / FLASH_SECTOR_ERASE_SIZE)
static_assert(FLASH_CLEARED_PAGES_BASE + (FLASH_MAX_CLEARED_PAGES / 32 - FLASH_VALID_BLOCKS_BASE <= FLASH_BITMAPS_SIZE),
              "");

static void _clear_bitset(uint32_t *mask, uint32_t count) {
    memset0(mask, count / 8);
}

static bool _update_current_uf2_info(struct uf2_block *uf2, uint32_t token) {
    bool ram = is_address_ram(uf2->target_addr) && is_address_ram(uf2->target_addr + (FLASH_PAGE_MASK));
    bool flash = is_address_flash(uf2->target_addr) && is_address_flash(uf2->target_addr + (FLASH_PAGE_MASK));
    if (!(uf2->num_blocks && (ram || flash)) || (flash && (uf2->target_addr & (FLASH_PAGE_MASK)))) {
        uf2_debug("Resetting active UF2 transfer because received garbage\n");
    } else if (!virtual_disk_queue.disable) {
        // note (test abive) if virtual disk queue is disabled (and note since we're in IRQ that cannot change whilst we are executing),
        // then we don't want to do any of this even if the task will be ignored later (doing this would modify our state)
        uint8_t type = AT_WRITE; // we always write
        if (_uf2_info.num_blocks != uf2->num_blocks) {
            // todo we may be able to skip some of these checks and let the task handle it (it will ignore garbage addresses for example)
            uf2_debug("Resetting active UF2 transfer because have new binary size %d->%d\n", (int) _uf2_info.num_blocks,
                      (int) uf2->num_blocks);
            memset0(&_uf2_info, sizeof(_uf2_info));
            _uf2_info.ram = ram;
            _uf2_info.valid_blocks = ram ? uf2_valid_ram_blocks : (uint32_t *) FLASH_VALID_BLOCKS_BASE;
            _uf2_info.max_valid_blocks = ram ? count_of(uf2_valid_ram_blocks) * 32 : FLASH_MAX_VALID_BLOCKS;
            uf2_debug("  ram %d, so valid_blocks (max %d) %p->%p for %dK\n", ram, (int) _uf2_info.max_valid_blocks,
                      _uf2_info.valid_blocks, _uf2_info.valid_blocks + ((_uf2_info.max_valid_blocks + 31) / 32),
                      (uint) _uf2_info.max_valid_blocks / 4);
            _clear_bitset(_uf2_info.valid_blocks, _uf2_info.max_valid_blocks);
            if (flash) {
                _uf2_info.cleared_pages = (uint32_t *) FLASH_CLEARED_PAGES_BASE;
                _uf2_info.max_cleared_pages = FLASH_MAX_CLEARED_PAGES;
                uf2_debug("    cleared_pages %p->%p\n", _uf2_info.cleared_pages,
                          _uf2_info.cleared_pages + ((_uf2_info.max_cleared_pages + 31) / 32));
                _clear_bitset(_uf2_info.cleared_pages, _uf2_info.max_cleared_pages);
            }

            if (uf2->num_blocks > _uf2_info.max_valid_blocks) {
                uf2_debug("Oops image requires %d blocks and won't fit", (uint) uf2->num_blocks);
                return false;
            }
            usb_warn("New UF2 transfer\n");
            _uf2_info.num_blocks = uf2->num_blocks;
            _uf2_info.valid_block_count = 0;
            _uf2_info.lowest_addr = 0xffffffff;
            if (flash) type |= AT_EXIT_XIP;
        }
        if (ram != _uf2_info.ram) {
            uf2_debug("Ignoring write to out of range address 0x%08x->0x%08x\n",
                      (uint) uf2->target_addr, (uint) (uf2->target_addr + uf2->payload_size));
        } else {
            assert(uf2->num_blocks <= _uf2_info.max_valid_blocks);
            if (uf2->block_no < uf2->num_blocks) {
                // set up next task state (also serves as a holder for state scoped to this block write to avoid copying data around)
                reset_task(&_uf2_info.next_task);
                _uf2_info.block_no = uf2->block_no;
                _uf2_info.token = _uf2_info.next_task.token = token;
                _uf2_info.next_task.transfer_addr = uf2->target_addr;
                _uf2_info.next_task.type = type;
                _uf2_info.next_task.data = uf2->data;
                _uf2_info.next_task.callback = _write_uf2_page_complete;
                _uf2_info.next_task.data_length = FLASH_PAGE_SIZE; // always a full page
                _uf2_info.next_task.source = TASK_SOURCE_VIRTUAL_DISK;
                return true;
            } else {
                uf2_debug("Ignoring write to out of range block %d >= %d\n", (int) uf2->block_no,
                          (int) uf2->num_blocks);
            }
        }
    }
    _uf2_info.num_blocks = 0; // invalid
    return false;
}

// note caller must pass SECTOR_SIZE buffer
bool vd_write_block(uint32_t token, __unused uint32_t lba, uint8_t *buf __comma_removed_for_space(uint32_t buf_size)) {
    struct uf2_block *uf2 = (struct uf2_block *) buf;
    if (uf2->magic_start0 == UF2_MAGIC_START0 && uf2->magic_start1 == UF2_MAGIC_START1 &&
        uf2->magic_end == UF2_MAGIC_END) {
        if (uf2->flags & UF2_FLAG_FAMILY_ID_PRESENT && uf2->file_size == RP2040_FAMILY_ID &&
            !(uf2->flags & UF2_FLAG_NOT_MAIN_FLASH) && uf2->payload_size == 256) {
            if (_update_current_uf2_info(uf2, token)) {
                // if we have a valid uf2 page, write it
                return _write_uf2_page();
            }
        } else {
            uf2_debug("Sector %d: ignoring write of non Mu UF2 sector\n", (uint) lba);
        }
    } else {
        uf2_debug("Sector %d: ignoring write of non UF2 sector\n", (uint) lba);
    }
    return false;
}
