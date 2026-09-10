/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/lockable.h"
#include "qemu/atomic.h"
#include "adb.h"
#define ADB_RING_SIZE 65536
static QemuMutex lock;
static int initialized, disconnect_requested;
static bool connected;
typedef struct Ring { uint8_t data[ADB_RING_SIZE]; size_t head, count; } Ring;
static Ring host_to_guest, guest_to_host;
static size_t ring_write(Ring *r, const uint8_t *data, size_t length)
{
    length = MIN(length, ADB_RING_SIZE - r->count);
    size_t tail = (r->head + r->count) % ADB_RING_SIZE;
    size_t first = MIN(length, ADB_RING_SIZE - tail);
    memcpy(r->data + tail, data, first); memcpy(r->data, data + first, length - first);
    r->count += length; return length;
}
static size_t ring_read(Ring *r, uint8_t *data, size_t length)
{
    length = MIN(length, r->count);
    size_t first = MIN(length, ADB_RING_SIZE - r->head);
    memcpy(data, r->data + r->head, first); memcpy(data + first, r->data, length - first);
    r->head = (r->head + length) % ADB_RING_SIZE; r->count -= length; return length;
}
void android51_adb_disconnect(void) { qatomic_set(&disconnect_requested, 1); }
bool gf_adb_take_disconnect(void) { return qatomic_xchg(&disconnect_requested, 0) != 0; }
void gf_adb_init(void) { qemu_mutex_init(&lock); qatomic_store_release(&initialized, 1); }
void gf_adb_connect(bool value)
{
    QEMU_LOCK_GUARD(&lock);
    connected = value;
    host_to_guest.head = host_to_guest.count = guest_to_host.head = guest_to_host.count = 0;
}
bool android51_adb_connected(void)
{
    if (!qatomic_load_acquire(&initialized)) { return false; }
    QEMU_LOCK_GUARD(&lock); return connected;
}
size_t android51_adb_read(uint8_t *data, size_t length)
{
    if (!data || !qatomic_load_acquire(&initialized)) { return 0; }
    QEMU_LOCK_GUARD(&lock); return connected ? ring_read(&guest_to_host, data, length) : 0;
}
size_t android51_adb_write(const uint8_t *data, size_t length)
{
    if (!data || !qatomic_load_acquire(&initialized)) { return 0; }
    QEMU_LOCK_GUARD(&lock); return connected ? ring_write(&host_to_guest, data, length) : 0;
}
size_t gf_adb_guest_send(const uint8_t *data, size_t length)
{
    QEMU_LOCK_GUARD(&lock); return connected ? ring_write(&guest_to_host, data, length) : 0;
}
size_t gf_adb_guest_receive(uint8_t *data, size_t length)
{
    QEMU_LOCK_GUARD(&lock); return connected ? ring_read(&host_to_guest, data, length) : 0;
}
size_t gf_adb_guest_writable(void)
{
    QEMU_LOCK_GUARD(&lock); return connected ? ADB_RING_SIZE - guest_to_host.count : 0;
}
