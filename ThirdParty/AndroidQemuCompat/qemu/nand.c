/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
*/
/* AndroidEmu port of nand.c/nand_reg.h: raw ext4 geometry, QEMU block
 * backends, bounded transfers, LE batch decoding, no host abort on bad MMIO.
 */
#include "qemu/osdep.h"
#include "android51.h"
#include "system/block-backend.h"
#include "system/reset.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "qapi/error.h"

typedef struct GFPartition {
    const char *name;
    BlockBackend *blk;
    uint64_t length;
    bool readonly;
} GFPartition;
typedef struct GFNand {
    Android51State *board;
    MemoryRegion io;
    GFPartition partitions[3];
    unsigned count;
    uint32_t dev, data, length, result;
    uint64_t address, batch;
    uint8_t buffer[65536];
} GFNand;

static uint32_t nand_command(GFNand *s, uint32_t cmd)
{
    GFPartition *p;
    uint64_t address;
    uint32_t remaining, completed = 0;
    if (cmd >= 6 && cmd <= 8) {
        uint8_t batch[24];
        if (s->batch > MACHINE(s->board)->ram_size - sizeof(batch) ||
            address_space_read(&address_space_memory, s->batch, MEMTXATTRS_UNSPECIFIED,
                               batch, sizeof(batch)) != MEMTX_OK) { return 0; }
        s->dev = ldl_le_p(batch);
        s->address = ldq_le_p(batch + 4);
        s->length = ldl_le_p(batch + 12);
        s->data = ldl_le_p(batch + 16);
        cmd -= 5; /* READ/WRITE/ERASE */
    }
    if (s->dev >= s->count || s->length > 16 * 1024 * 1024) { return 0; }
    p = &s->partitions[s->dev];
    if (cmd == 0) {
        size_t length = MIN(s->length, strlen(p->name));
        return gf_guest_virtual(s->board, s->data, (void *)p->name, length, true) ? length : 0;
    }
    if (cmd == 4 || cmd == 5) { return 0; } /* File-backed media have no bad erase blocks. */
    if (cmd < 1 || cmd > 3 || (cmd != 1 && p->readonly) || s->address >= p->length) { return 0; }
    remaining = MIN((uint64_t)s->length, p->length - s->address);
    if (cmd != 3 && remaining > (1ULL << 32) - s->data) { return 0; }
    address = s->address;
    if (cmd == 3) { memset(s->buffer, 0xff, sizeof(s->buffer)); }
    while (remaining) {
        size_t bytes = MIN(remaining, sizeof(s->buffer));
        if (cmd == 1) {
            if (blk_pread(p->blk, address, bytes, s->buffer, 0) < 0 ||
                !gf_guest_virtual(s->board, s->data + completed, s->buffer, bytes, true)) { break; }
        } else {
            if (cmd == 2 && !gf_guest_virtual(s->board, s->data + completed, s->buffer, bytes, false)) { break; }
            if (blk_pwrite(p->blk, address, bytes, s->buffer, 0) < 0) { break; }
        }
        address += bytes;
        remaining -= bytes;
        completed += bytes;
    }
    return completed;
}
static uint64_t nand_read(void *opaque, hwaddr offset, unsigned size)
{
    GFNand *s = opaque;
    GFPartition *p;
    switch (offset) {
    case 0: return 1;
    case 4: return s->count;
    case 0x40: return s->result;
    }
    if (s->dev >= s->count) { return 0; }
    p = &s->partitions[s->dev];
    switch (offset) {
    case 0x10: return (p->readonly ? 1 : 0) | 2; /* READ_ONLY | BATCH_CAP */
    case 0x14: return strlen(p->name);
    case 0x18: return 512;
    case 0x1c: return 0; /* Raw ext4 image has no NAND OOB bytes. */
    case 0x20: return 4096;
    case 0x28: return (uint32_t)p->length;
    case 0x2c: return p->length >> 32;
    default: return 0;
    }
}
static void nand_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    GFNand *s = opaque;
    switch (offset) {
    case 8: s->dev = value; break;
    case 0x48: s->data = value; break;
    case 0x4c: s->length = value; break;
    case 0x50: s->address = (s->address & 0xffffffff00000000ULL) | (uint32_t)value; break;
    case 0x54: s->address = (s->address & UINT32_MAX) | (value << 32); break;
    case 0x58: s->batch = (s->batch & 0xffffffff00000000ULL) | (uint32_t)value; break;
    case 0x5c: s->batch = (s->batch & UINT32_MAX) | (value << 32); break;
    case 0x44:
        s->result = nand_command(s, value);
        if (value >= 6 && value <= 8 && s->batch <= MACHINE(s->board)->ram_size - 24) {
            uint32_t result = cpu_to_le32(s->result);
            /* Preserve the descriptor inputs. Old AOSP code wrote uninitialized bytes here. */
            address_space_write(&address_space_memory, s->batch + 20,
                                MEMTXATTRS_UNSPECIFIED, &result, sizeof(result));
        }
        break;
    }
}
static const MemoryRegionOps nand_ops = {
    .read = nand_read, .write = nand_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
};
static void nand_reset(void *opaque)
{
    GFNand *s = opaque;
    s->dev = s->data = s->length = s->result = 0;
    s->address = s->batch = 0;
}
void gf_nand_init(Android51State *board)
{
    GFNand *s = g_new0(GFNand, 1);
    static const char *names[] = {"system", "userdata", "cache"};
    s->board = board;
    for (unsigned i = 0; i < ARRAY_SIZE(names); ++i) {
        BlockBackend *blk = blk_by_name(names[i]);
        if (blk) {
            int64_t length = blk_getlength(blk);
            if (length <= 0 || length > 8ULL * 1024 * 1024 * 1024 || length % 4096) {
                error_report("android51: %s must be a 4-KiB aligned raw image <= 8 GiB", names[i]);
                exit(EXIT_FAILURE);
            }
            bool readonly = i == 0 || !blk_supports_write_perm(blk);
            blk_set_perm(blk, BLK_PERM_CONSISTENT_READ | (readonly ? 0 : BLK_PERM_WRITE),
                         BLK_PERM_CONSISTENT_READ, &error_fatal);
            s->partitions[s->count++] = (GFPartition){names[i], blk, length, readonly};
        }
    }
    gf_map(board, &s->io, "android51.nand", 0xff030000, &nand_ops, s);
    gf_register(board, "goldfish_nand", 0, 0xff030000, 0x1000, 0, 0);
    qemu_register_reset(nand_reset, s);
}
