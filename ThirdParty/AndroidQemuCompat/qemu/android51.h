/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ANDROID51_GOLDFISH_H
#define ANDROID51_GOLDFISH_H
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "target/arm/cpu.h"
#include "hw/irq.h"
#include "exec/address-spaces.h"
#include "qemu/timer.h"
#include "chardev/char-fe.h"

#define TYPE_ANDROID51_MACHINE MACHINE_TYPE_NAME("android51")
OBJECT_DECLARE_SIMPLE_TYPE(Android51State, ANDROID51_MACHINE)
#define GF_MAX_DEVICES 24
typedef struct GFDevice {
    const char *name;
    uint32_t base, size, irq, irq_count;
    int32_t id;
} GFDevice;
struct Android51State {
    MachineState parent_obj;
    ARMCPU *cpu;
    uint32_t width, height;
    struct arm_boot_info boot;
    MemoryRegion pic_io, bus_io, tty_io, timer_io, rtc_io;
    qemu_irq *irqs;
    uint32_t levels, enabled;
    GFDevice devices[GF_MAX_DEVICES];
    unsigned device_count, bus_cursor;
    uint32_t bus_name_high;
    CharBackend tty;
    uint32_t tty_ptr, tty_length, tty_count;
    bool tty_irq_enabled;
    uint8_t tty_rx[4096];
    QEMUTimer *alarm;
    uint64_t latched_time, alarm_time, rtc_time;
    void *display_state, *events_state;
};
void gf_register(Android51State *s, const char *name, int32_t id,
                 uint32_t base, uint32_t size, uint32_t irq, uint32_t irq_count);
void gf_map(Android51State *s, MemoryRegion *mr, const char *name, uint32_t base,
            const MemoryRegionOps *ops, void *opaque);
bool gf_guest_virtual(Android51State *s, uint32_t address, void *buffer, size_t length, bool write);
void gf_platform_init(Android51State *s);
void gf_nand_init(Android51State *s);
void gf_display_init(Android51State *s);
void gf_events_init(Android51State *s);
void gf_battery_init(Android51State *s);
void gf_pipes_init(Android51State *s);
void gf_audio_init(Android51State *s);
bool android51_input_event(uint16_t type, uint16_t code, int32_t value);
#endif
