/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANDROID51_ADB_H
#define ANDROID51_ADB_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* In-process transport only. No network listening socket. Thread-safe partial
 * reads/writes; zero means backpressure or disconnected, connected() disambiguates. */
bool android51_adb_connected(void);
void android51_adb_disconnect(void);
size_t android51_adb_read(uint8_t *, size_t);
size_t android51_adb_write(const uint8_t *, size_t);
#ifdef __cplusplus
}
#endif
/* QEMU device-thread methods, not part of the application ABI. */
void gf_adb_init(void);
bool gf_adb_take_disconnect(void);
void gf_adb_connect(bool);
size_t gf_adb_guest_send(const uint8_t *, size_t);
size_t gf_adb_guest_receive(uint8_t *, size_t);
size_t gf_adb_guest_writable(void);
#endif
