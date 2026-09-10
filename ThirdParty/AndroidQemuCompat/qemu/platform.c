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
/* AndroidEmu port: device.c, interrupt.c, tty.c, timer.c. Bounds-checked
 * MMIO and guest virtual transfers; current chardev and timer APIs.
 */
#include "qemu/osdep.h"
#include "android51.h"
#include "android51_host.h"
#include "system/system.h"
#include "system/reset.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"

static void pic_update(Android51State *s)
{
    qemu_set_irq(qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ), (s->levels & s->enabled) != 0);
}
static void pic_input(void *opaque, int irq, int level)
{
    Android51State *s = opaque;
    uint32_t bit = 1U << irq;
    s->levels = level ? s->levels | bit : s->levels & ~bit;
    pic_update(s);
}
static uint64_t pic_read(void *opaque, hwaddr offset, unsigned size)
{
    Android51State *s = opaque;
    uint32_t pending = s->levels & s->enabled;
    switch (offset) {
    case 0: return ctpop32(pending);
    case 4: return pending ? ctz32(pending) : 0; /* v1 returns IRQ NUMBER, not mask. */
    default: return 0;
    }
}
static void pic_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    Android51State *s = opaque;
    switch (offset) {
    case 8: s->levels = 0; s->enabled = 0; break;
    case 12: if (value < 32) { s->enabled &= ~(1U << value); } break;
    case 16: if (value < 32) { s->enabled |= 1U << value; } break;
    }
    pic_update(s);
}
static const MemoryRegionOps pic_ops = {
    .read = pic_read, .write = pic_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
};

static GFDevice *bus_current(Android51State *s)
{
    return s->bus_cursor && s->bus_cursor <= s->device_count ? &s->devices[s->bus_cursor - 1] : NULL;
}
static uint64_t bus_read(void *opaque, hwaddr offset, unsigned size)
{
    Android51State *s = opaque;
    GFDevice *d;
    if (offset == 0) {
        if (s->bus_cursor < s->device_count) { ++s->bus_cursor; return 8; }
        s->bus_cursor = s->device_count + 1;
        qemu_irq_lower(s->irqs[1]);
        return 0;
    }
    d = bus_current(s);
    if (!d) { return 0; }
    switch (offset) {
    case 8: return strlen(d->name);
    case 12: return (uint32_t)d->id;
    case 16: return d->base;
    case 20: return d->size;
    case 24: return d->irq;
    case 28: return d->irq_count;
    default: return 0;
    }
}
static void bus_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    Android51State *s = opaque;
    GFDevice *d = bus_current(s);
    if (offset == 0 && value == 0) {
        s->bus_cursor = 0;
        qemu_set_irq(s->irqs[1], s->device_count != 0);
    } else if (offset == 4 && d) {
        gf_guest_virtual(s, value, (void *)d->name, strlen(d->name), true);
    }
}
static const MemoryRegionOps bus_ops = {
    .read = bus_read, .write = bus_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
};

static void tty_update(Android51State *s)
{
    qemu_set_irq(s->irqs[4], s->tty_irq_enabled && s->tty_count != 0);
}
static int tty_can_receive(void *opaque)
{
    Android51State *s = opaque;
    return sizeof(s->tty_rx) - s->tty_count;
}
static void tty_receive(void *opaque, const uint8_t *data, int length)
{
    Android51State *s = opaque;
    if (length < 0 || length > tty_can_receive(s)) { return; }
    memcpy(s->tty_rx + s->tty_count, data, length);
    s->tty_count += length;
    tty_update(s);
}
static uint64_t tty_read(void *opaque, hwaddr offset, unsigned size)
{
    Android51State *s = opaque;
    return offset == 4 ? s->tty_count : 0;
}
static void tty_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    Android51State *s = opaque;
    switch (offset) {
    case 0: {
        uint8_t ch = value;
        qemu_chr_fe_write(&s->tty, &ch, 1);
        android51_host_serial(&ch, 1);
        break;
    }
    case 16: s->tty_ptr = value; break;
    case 20: s->tty_length = value; break;
    case 8:
        switch (value) {
        case 0: s->tty_irq_enabled = false; break;
        case 1: s->tty_irq_enabled = true; break;
        case 2: {
            uint8_t data[4096];
            size_t consumed = 0;
            /* Bound work per guest MMIO request and reject wrapping vaddrs. */
            if (s->tty_length > 65536 || s->tty_length > (1ULL << 32) - s->tty_ptr) { break; }
            while (consumed < s->tty_length) {
                size_t count = MIN(sizeof(data), s->tty_length - consumed);
                if (!gf_guest_virtual(s, s->tty_ptr + consumed, data, count, false)) { break; }
                qemu_chr_fe_write(&s->tty, data, count);
                android51_host_serial(data, count);
                consumed += count;
            }
            break;
        }
        case 3:
            if (s->tty_length <= s->tty_count &&
                gf_guest_virtual(s, s->tty_ptr, s->tty_rx, s->tty_length, true)) {
                s->tty_count -= s->tty_length;
                memmove(s->tty_rx, s->tty_rx + s->tty_length, s->tty_count);
                qemu_chr_fe_accept_input(&s->tty);
            }
            break;
        }
        tty_update(s);
        break;
    }
}
static const MemoryRegionOps tty_ops = {
    .read = tty_read, .write = tty_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 4},
};

static void alarm_tick(void *opaque)
{
    Android51State *s = opaque;
    qemu_irq_raise(s->irqs[3]);
}
static uint64_t timer_read(void *opaque, hwaddr offset, unsigned size)
{
    Android51State *s = opaque;
    if (offset == 0) { s->latched_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL); return (uint32_t)s->latched_time; }
    return offset == 4 ? s->latched_time >> 32 : 0;
}
static void timer_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    Android51State *s = opaque;
    switch (offset) {
    case 8:
        s->alarm_time = (s->alarm_time & 0xffffffff00000000ULL) | (uint32_t)value;
        if (s->alarm_time <= qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
            timer_del(s->alarm); qemu_irq_raise(s->irqs[3]);
        } else if (s->alarm_time <= INT64_MAX) { timer_mod(s->alarm, s->alarm_time); }
        break;
    case 12: s->alarm_time = (value << 32) | (uint32_t)s->alarm_time; break;
    case 16: qemu_irq_lower(s->irqs[3]); break;
    case 20: timer_del(s->alarm); qemu_irq_lower(s->irqs[3]); break;
    }
}
static const MemoryRegionOps timer_ops = {
    .read = timer_read, .write = timer_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
};
static uint64_t rtc_read(void *opaque, hwaddr offset, unsigned size)
{
    Android51State *s = opaque;
    if (offset == 0) { s->rtc_time = qemu_clock_get_ns(QEMU_CLOCK_HOST); return (uint32_t)s->rtc_time; }
    return offset == 4 ? s->rtc_time >> 32 : 0;
}
static void rtc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    /* The API 22 goldfish_rtc driver only needs wall-clock reads. */
    Android51State *s = opaque;
    if (offset == 16) { qemu_irq_lower(s->irqs[11]); }
}
static const MemoryRegionOps rtc_ops = {
    .read = rtc_read, .write = rtc_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4},
};
static void platform_reset(void *opaque)
{
    Android51State *s = opaque;
    s->enabled = s->levels = 0;
    s->bus_cursor = 0;
    s->tty_ptr = s->tty_length = s->tty_count = 0;
    s->tty_irq_enabled = false;
    s->latched_time = s->alarm_time = s->rtc_time = 0;
    timer_del(s->alarm);
    pic_update(s);
}
void gf_platform_init(Android51State *s)
{
    s->irqs = qemu_allocate_irqs(pic_input, s, 32);
    gf_map(s, &s->pic_io, "android51.pic", 0xff000000, &pic_ops, s);
    gf_register(s, "goldfish_interrupt_controller", -1, 0xff000000, 0x1000, 0, 0);
    gf_map(s, &s->bus_io, "android51.bus", 0xff001000, &bus_ops, s);
    gf_register(s, "goldfish_device_bus", -1, 0xff001000, 0x1000, 1, 1);
    qemu_chr_fe_init(&s->tty, serial_hd(0), &error_fatal);
    qemu_chr_fe_set_handlers(&s->tty, tty_can_receive, tty_receive, NULL, NULL, s, NULL, true);
    gf_map(s, &s->tty_io, "android51.tty", 0xff002000, &tty_ops, s);
    gf_register(s, "goldfish_tty", 0, 0xff002000, 0x1000, 4, 1);
    s->alarm = timer_new_ns(QEMU_CLOCK_VIRTUAL, alarm_tick, s);
    gf_map(s, &s->timer_io, "android51.timer", 0xff003000, &timer_ops, s);
    gf_register(s, "goldfish_timer", -1, 0xff003000, 0x1000, 3, 1);
    gf_map(s, &s->rtc_io, "android51.rtc", 0xff010000, &rtc_ops, s);
    gf_register(s, "goldfish_rtc", -1, 0xff010000, 0x1000, 11, 1);
    qemu_register_reset(platform_reset, s);
}
