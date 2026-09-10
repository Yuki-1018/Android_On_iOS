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
/* AndroidEmu: QEMU 10 port of goldfish/fb.c. Fixed BGRA32 540x960,
 * guest-RAM dirty tracking, virtual-clock vsync independent of host UI.
 */
#include "qemu/osdep.h"
#include "android51.h"
#include "android51_host.h"
#include "ui/console.h"
#include "exec/ram_addr.h"
#include "system/reset.h"

#define GF_WIDTH (s->board->width)
#define GF_HEIGHT (s->board->height)
typedef struct GFDisplay {
    Android51State *board;
    MemoryRegion io;
    QemuConsole *console;
    QEMUTimer *vsync;
    uint32_t base, status, enabled;
    bool valid, blank, invalidate, base_pending;
} GFDisplay;
static void display_irq(GFDisplay *s)
{
    qemu_set_irq(s->board->irqs[12], (s->status & s->enabled) != 0);
}
static void display_update(void *opaque)
{
    GFDisplay *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->console);
    MemoryRegion *ram = MACHINE(s->board)->ram;
    uint64_t bytes = GF_WIDTH * GF_HEIGHT * 4;
    int first = -1, last = -1;
    DirtyBitmapSnapshot *snapshot;
    uint8_t *source;
    if (!s->valid || s->base > memory_region_size(ram) || bytes > memory_region_size(ram) - s->base) { return; }
    source = memory_region_get_ram_ptr(ram) + s->base;
    snapshot = memory_region_snapshot_and_clear_dirty(ram, s->base, bytes, DIRTY_MEMORY_VGA);
    for (uint32_t y = 0; y < GF_HEIGHT; ++y) {
        uint64_t offset = (uint64_t)y * GF_WIDTH * 4;
        if (s->invalidate || memory_region_snapshot_get_dirty(ram, snapshot, s->base + offset, GF_WIDTH * 4)) {
            uint8_t *dest = surface_data(surface) + y * surface_stride(surface);
            if (s->blank) { memset(dest, 0, GF_WIDTH * 4); }
            else { memcpy(dest, source + offset, GF_WIDTH * 4); }
            if (first < 0) { first = y; }
            last = y;
        }
    }
    g_free(snapshot);
    s->invalidate = false;
    if (first >= 0) {
        dpy_gfx_update(s->console, 0, first, GF_WIDTH, last - first + 1);
        android51_host_frame(surface_data(surface), surface_stride(surface), 0, first, GF_WIDTH, last - first + 1);
    }
}
static void display_invalidate(void *opaque) { ((GFDisplay *)opaque)->invalidate = true; }
static const GraphicHwOps graphic_ops = { .gfx_update = display_update, .invalidate = display_invalidate };
static void display_tick(void *opaque)
{
    GFDisplay *s = opaque;
    display_update(s);
    s->status |= 1;
    if (s->base_pending) { s->status |= 2; s->base_pending = false; }
    display_irq(s);
    timer_mod(s->vsync, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 16666667);
}
static uint64_t display_read(void *opaque, hwaddr offset, unsigned size)
{
    GFDisplay *s = opaque;
    switch (offset) {
    case 0: return GF_WIDTH;
    case 4: return GF_HEIGHT;
    case 8: {
        uint32_t status = s->status & s->enabled;
        s->status &= ~status;
        display_irq(s);
        return status;
    }
    case 0x1c: return GF_WIDTH * 254 / 1600;
    case 0x20: return GF_HEIGHT * 254 / 1600;
    case 0x24: return 5; /* HAL_PIXEL_FORMAT_BGRA_8888 */
    default: return 0;
    }
}
static void display_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    GFDisplay *s = opaque;
    switch (offset) {
    case 12: s->enabled = value & 3; break;
    case 16:
        s->base = value;
        s->valid = true;
        s->invalidate = true;
        s->base_pending = true;
        s->status &= ~2U;
        break;
    case 20: /* SurfaceFlinger performs rotation; physical panel stays portrait. */ break;
    case 24: s->blank = value != 0; s->invalidate = true; break;
    }
    display_irq(s);
}
static const MemoryRegionOps display_ops = {
    .read = display_read, .write = display_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
};
static void display_reset(void *opaque)
{
    GFDisplay *s = opaque;
    s->base = s->status = s->enabled = 0;
    s->valid = s->blank = s->base_pending = false;
    s->invalidate = true;
    display_irq(s);
    timer_mod(s->vsync, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 16666667);
}
void gf_display_init(Android51State *board)
{
    GFDisplay *s = g_new0(GFDisplay, 1);
    s->board = board;
    s->console = graphic_console_init(NULL, 0, &graphic_ops, s);
    qemu_console_resize(s->console, GF_WIDTH, GF_HEIGHT);
    s->vsync = timer_new_ns(QEMU_CLOCK_VIRTUAL, display_tick, s);
    memory_region_set_log(MACHINE(board)->ram, true, DIRTY_MEMORY_VGA);
    gf_map(board, &s->io, "android51.fb", 0xff040000, &display_ops, s);
    gf_register(board, "goldfish_fb", 0, 0xff040000, 0x1000, 12, 1);
    board->display_state = s;
    qemu_register_reset(display_reset, s);
}
