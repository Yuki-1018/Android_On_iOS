/* Derived from StikDebug/StikJIT INTEGRATION.md, revision
 * 3623e725876f76aecb0520582ad6194bacb15d39.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. See LICENSE in this directory.
 * Changes: isolate the two app-side arm64 protocol entry points in C.
 */
#include <stddef.h>
#if defined(__aarch64__)
__attribute__((noinline, optnone, naked))
void JIT26Detach(void) {
    __asm__("mov x16, #0\n" "brk #0xf00d\n" "ret\n");
}
__attribute__((noinline, optnone, naked))
void *JIT26PrepareRegion(void *address __attribute__((unused)), size_t length __attribute__((unused))) {
    __asm__("mov x16, #1\n" "brk #0xf00d\n" "ret\n");
}
#else
#error This app requires an arm64 iPhoneOS target.
#endif
