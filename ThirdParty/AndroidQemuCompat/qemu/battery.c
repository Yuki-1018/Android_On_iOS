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
/* AndroidEmu: minimal constant AC/100% battery, real MMIO/IRQ protocol. */
#include "qemu/osdep.h"
#include "android51.h"
#include "system/reset.h"
typedef struct GFBattery { Android51State *board; MemoryRegion io; uint32_t enabled, status; } GFBattery;
static void battery_irq(GFBattery *s) { qemu_set_irq(s->board->irqs[14], (s->enabled & s->status) != 0); }
static uint64_t battery_read(void *opaque, hwaddr offset, unsigned size)
{
    GFBattery *s = opaque;
    switch (offset) {
    case 0: { uint32_t status = s->status; s->status = 0; battery_irq(s); return status; }
    case 4: return s->enabled;
    case 8: return 1;  /* AC online */
    case 12: return 4; /* POWER_SUPPLY_STATUS_FULL */
    case 16: return 1; /* POWER_SUPPLY_HEALTH_GOOD */
    case 20: return 1;
    case 24: return 100;
    default: return 0;
    }
}
static void battery_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    GFBattery *s = opaque;
    if (offset == 4) { s->enabled = value & 3; battery_irq(s); }
}
static const MemoryRegionOps battery_ops = {
    .read = battery_read, .write = battery_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
};
static void battery_reset(void *opaque)
{
    GFBattery *s = opaque;
    s->enabled = 0; s->status = 3; battery_irq(s);
}
void gf_battery_init(Android51State *board)
{
    GFBattery *s = g_new0(GFBattery, 1);
    s->board = board;
    gf_map(board, &s->io, "android51.battery", 0xff060000, &battery_ops, s);
    gf_register(board, "goldfish-battery", 0, 0xff060000, 0x1000, 14, 1);
    qemu_register_reset(battery_reset, s);
}
