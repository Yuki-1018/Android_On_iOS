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
/* AndroidEmu: port of android_arm.c to QEMU 10 MachineState/MemoryRegion.
 * Fixed ARMv7 Goldfish profile; no modem, USB, sensors, or migration.
 */
#include "qemu/osdep.h"
#include "android51.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/net/smc91c111.h"
#include "exec/cpu-common.h"

void gf_register(Android51State *s, const char *name, int32_t id,
                 uint32_t base, uint32_t size, uint32_t irq, uint32_t irq_count)
{
    g_assert(s->device_count < GF_MAX_DEVICES);
    g_assert(irq < 32 && irq_count <= 32 - irq);
    s->devices[s->device_count++] = (GFDevice){name, base, size, irq, irq_count, id};
}

void gf_map(Android51State *s, MemoryRegion *mr, const char *name, uint32_t base,
            const MemoryRegionOps *ops, void *opaque)
{
    memory_region_init_io(mr, OBJECT(s), ops, opaque, name, 0x1000);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

bool gf_guest_virtual(Android51State *s, uint32_t address, void *buffer,
                      size_t length, bool write)
{
    /* Goldfish v1 passes guest VIRTUAL pointers, even for NAND batch payloads.
     * qtest executes with the MMU off; cpu_memory_rw_debug still uses the CPU.
     */
    if (length > INT_MAX || length > (1ULL << 32) - address) {
        return false;
    }
    /* Validate every translation before accessing anything: debug memory I/O
     * also accepts MMIO, which could recursively enter this device via DMA. */
    for (uint64_t cursor = address, end = cursor + length; cursor < end;) {
        hwaddr page = cpu_get_phys_page_debug(CPU(s->cpu), cursor & TARGET_PAGE_MASK);
        size_t chunk = MIN(end - cursor, TARGET_PAGE_SIZE - (cursor & ~TARGET_PAGE_MASK));
        uint64_t physical = page + (cursor & ~TARGET_PAGE_MASK);
        if (page == (hwaddr)-1 || physical >= MACHINE(s)->ram_size ||
            chunk > MACHINE(s)->ram_size - physical) {
            return false;
        }
        cursor += chunk;
    }
    return cpu_memory_rw_debug(CPU(s->cpu), address, buffer, length, write) == 0;
}

static void android51_init(MachineState *machine)
{
    Android51State *s = ANDROID51_MACHINE(machine);
    if (machine->ram_size < 128 * MiB || machine->ram_size > GiB) {
        error_report("android51: RAM must be between 128 and 1024 MiB");
        exit(EXIT_FAILURE);
    }
    if (s->width < 320 || s->width > 720 || s->height < 480 || s->height > 1600 || s->width % 4 || s->height % 2) {
        error_report("android51: unsupported display dimensions"); exit(EXIT_FAILURE);
    }
    s->cpu = ARM_CPU(cpu_create(machine->cpu_type));
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);
    gf_platform_init(s);
    gf_register(s, "smc91x", 0, 0xff020000, 0x1000, 10, 1);
    smc91c111_init(0xff020000, s->irqs[10]);
    gf_nand_init(s);
    gf_display_init(s);
    gf_events_init(s);
    gf_battery_init(s);
    gf_pipes_init(s);
    gf_audio_init(s);
    s->boot.ram_size = machine->ram_size;
    s->boot.loader_start = 0;
    s->boot.board_id = 1441;
    arm_load_kernel(s->cpu, machine, &s->boot);
}

static void panel_get(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    Android51State *s = ANDROID51_MACHINE(obj);
    uint32_t value = !strcmp(name, "width") ? s->width : s->height;
    visit_type_uint32(v, name, &value, errp);
}
static void panel_set(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp)
{
    Android51State *s = ANDROID51_MACHINE(obj);
    visit_type_uint32(v, name, !strcmp(name, "width") ? &s->width : &s->height, errp);
}
static void android51_instance_init(Object *obj)
{
    Android51State *s = ANDROID51_MACHINE(obj);
    s->width = 540; s->height = 960;
}
static void android51_class_init(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    mc->desc = "Android 5.1.1 ARMv7 Goldfish (development)";
    mc->init = android51_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a8");
    mc->default_ram_id = "android51.ram";
    mc->default_ram_size = 640 * MiB;
    /* The stock board/kernel uses a single Goldfish PIC and no SMP boot path. */
    mc->max_cpus = 1;
    mc->default_cpus = 1;
    mc->no_parallel = true;
    mc->no_floppy = true;
    mc->no_cdrom = true;
    machine_add_audiodev_property(mc);
    object_class_property_add(klass, "width", "uint32", panel_get, panel_set, NULL, NULL);
    object_class_property_add(klass, "height", "uint32", panel_get, panel_set, NULL, NULL);
}
static const TypeInfo android51_type = {
    .name = TYPE_ANDROID51_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Android51State),
    .class_init = android51_class_init,
    .instance_init = android51_instance_init,
};
static void android51_register_types(void) { type_register_static(&android51_type); }
type_init(android51_register_types)
