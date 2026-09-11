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
/* qwerty2 selects the stock API22 IDC: touch.deviceType=touchScreen.
 * The goldfish 3.4 driver does not import INPUT_PROP_DIRECT.
 * AndroidEmu port of events_device.c; touchscreen-only Protocol B,
 * 10 slots, bounded native injection, no mouse/trackball translation.
 */
#include "qemu/osdep.h"
#include "android51.h"
#include "standard-headers/linux/input.h"
#include "system/reset.h"
#include "qemu/main-loop.h"

typedef struct GFEvents {
    Android51State *board;
    MemoryRegion io;
    uint32_t page, first, count;
    uint32_t queue[4096];
    uint8_t bits[EV_MAX + 1][(KEY_MAX + 8) / 8];
    uint32_t absinfo[(ABS_MAX + 1) * 4];
    bool live;
} GFEvents;
static GFEvents *active_events;
static const char device_name[] = "qwerty2";
static void events_irq(GFEvents *s)
{
    qemu_set_irq(s->board->irqs[13], s->live && s->count != 0);
}
/* Caller must hold QEMU's BQL. The iOS bridge drains its SPSC queue here. */
bool android51_input_event(uint16_t type, uint16_t code, int32_t value)
{
    GFEvents *s = active_events;
    g_assert(bql_locked());
    if (!s || s->count > ARRAY_SIZE(s->queue) - 3 || type > EV_MAX || code > KEY_MAX) { return false; }
    if (!(s->bits[type][code / 8] & (1U << (code % 8)))) { return false; }
    uint32_t words[] = {type, code, (uint32_t)value};
    for (unsigned i = 0; i < 3; ++i) {
        s->queue[(s->first + s->count) % ARRAY_SIZE(s->queue)] = words[i];
        ++s->count;
    }
    events_irq(s);
    return true;
}
static uint32_t page_length(GFEvents *s)
{
    if (s->page == 0) { return strlen(device_name); }
    if (s->page == (0x20000 | EV_ABS)) { return sizeof(s->absinfo); }
    if (s->page >= 0x10000 && s->page <= 0x10000 + EV_MAX) {
        unsigned type = s->page - 0x10000;
        unsigned length = sizeof(s->bits[type]);
        while (length && s->bits[type][length - 1] == 0) { --length; }
        return length;
    }
    return 0;
}
static uint64_t events_read(void *opaque, hwaddr offset, unsigned size)
{
    GFEvents *s = opaque;
    if (offset == 0) {
        uint32_t value = 0;
        if (s->count) { value = s->queue[s->first]; s->first = (s->first + 1) % ARRAY_SIZE(s->queue); --s->count; }
        events_irq(s);
        return value;
    }
    if (offset == 4) {
        if (s->page == (0x20000 | EV_ABS)) { s->live = true; events_irq(s); }
        return page_length(s);
    }
    if (offset < 8 || offset - 8 >= page_length(s)) { return 0; }
    offset -= 8;
    if (s->page == 0) { return device_name[offset]; }
    if (s->page == (0x20000 | EV_ABS)) { return s->absinfo[offset / 4]; }
    return s->bits[s->page - 0x10000][offset];
}
static void events_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    GFEvents *s = opaque;
    if (offset == 0) { s->page = value; }
}
static const MemoryRegionOps events_ops = {
    .read = events_read, .write = events_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 4},
};
static void set_event(GFEvents *s, unsigned type, unsigned code)
{
    s->bits[0][type / 8] |= 1U << (type % 8);
    s->bits[type][code / 8] |= 1U << (code % 8);
}
static void set_axis(GFEvents *s, unsigned code, uint32_t minimum, uint32_t maximum)
{
    set_event(s, EV_ABS, code);
    s->absinfo[code * 4] = minimum;
    s->absinfo[code * 4 + 1] = maximum;
}
static void events_reset(void *opaque)
{
    GFEvents *s = opaque;
    s->first = s->count = s->page = 0;
    s->live = false;
    events_irq(s);
}
void gf_events_init(Android51State *board)
{
    GFEvents *s = g_new0(GFEvents, 1);
    static const unsigned keys[] = {KEY_BACK, KEY_HOME, KEY_POWER,
                                   KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_MENU, KEY_SEARCH, BTN_TOUCH};
    s->board = board;
    set_event(s, EV_SYN, SYN_REPORT);
    /* Do not advertise a physical alphabetic keyboard: Android must show its
     * on-screen keyboard for a touch-only device. */
    for (unsigned i = 0; i < ARRAY_SIZE(keys); ++i) { set_event(s, EV_KEY, keys[i]); }
    set_axis(s, ABS_X, 0, board->width - 1);
    set_axis(s, ABS_Y, 0, board->height - 1);
    set_axis(s, ABS_MT_SLOT, 0, 9);
    set_axis(s, ABS_MT_TRACKING_ID, 0, INT32_MAX);
    set_axis(s, ABS_MT_POSITION_X, 0, board->width - 1);
    set_axis(s, ABS_MT_POSITION_Y, 0, board->height - 1);
    gf_map(board, &s->io, "android51.events", 0xff050000, &events_ops, s);
    gf_register(board, "goldfish_events", 0, 0xff050000, 0x1000, 13, 1);
    board->events_state = active_events = s;
    qemu_register_reset(events_reset, s);
}
