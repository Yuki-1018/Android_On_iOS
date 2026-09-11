#include "QEMUBridge/NativeBridge.h"
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

extern "C" void *JIT26PrepareRegion(void*, size_t);
extern "C" void JIT26Detach(void);
namespace {
std::mutex arenaMutex;
std::atomic<bool> ready{false};
// The iOS vm_* APIs use native-width addresses on arm64.
static_assert(sizeof(vm_address_t) == sizeof(void*), "JIT addresses must not truncate pointers");
static_assert(sizeof(vm_size_t) == sizeof(size_t), "JIT sizes must not truncate allocations");
vm_address_t rx = 0, rw = 0;
size_t arenaBytes = 0;
bool attempted = false;
struct ProtocolDetach {
    bool active = false;
    void finish() { if (active) { JIT26Detach(); active = false; } }
    ~ProtocolDetach() { finish(); }
};
bool debuggerAttached() {
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    kinfo_proc info{}; size_t length = sizeof(info);
    return sysctl(mib, 4, &info, &length, nullptr, 0) == 0 && (info.kp_proc.p_flag & P_TRACED);
}
}
bool AEPrepareJITArena(size_t bytes, bool needsProtocol, char* error, size_t capacity) {
    std::lock_guard lock(arenaMutex);
    auto fail = [&](const char* message) {
        if (error && capacity) std::snprintf(error, capacity, "%s", message);
        if (rw) { vm_deallocate(mach_task_self(), rw, arenaBytes); rw = 0; }
        if (rx) { vm_deallocate(mach_task_self(), rx, arenaBytes); rx = 0; }
        arenaBytes = 0; return false;
    };
    if (ready.load(std::memory_order_acquire)) {
        if (bytes == arenaBytes) return true;
        if (error && capacity) std::snprintf(error, capacity, "Prepared cache size cannot change; restart the app");
        return false;
    }
    if (attempted) return fail("JIT preparation already attempted; restart the app to retry");
    if (!AEHasGetTaskAllow()) return fail("get-task-allow entitlement missing");
    const int txm = AETXMPresence(), sptm = AESPTMPresence();
    if (txm < 0 || sptm < 0) return fail("TXM/SPTM detection unavailable");
    if (needsProtocol != (txm == 1 || sptm == 1)) return fail("JIT protocol does not match detected memory protection");
    if (!AEIsDebugged()) return fail("Debugger has not enabled JIT");
    if (needsProtocol && !debuggerAttached()) return fail("Universal debugger script must remain attached during preparation");
    if (bytes != 128UL << 20 && bytes != 192UL << 20 && bytes != 256UL << 20) return fail("Unsupported TCG cache size");
    attempted = true;
    arenaBytes = bytes;
    ProtocolDetach detach;
    if (needsProtocol) {
        rx = reinterpret_cast<vm_address_t>(JIT26PrepareRegion(nullptr, bytes));
        detach.active = true;
        if (!rx || rx % vm_page_size) { rx = 0; return fail("Universal protocol returned an invalid RX region"); }
    } else {
        if (vm_allocate(mach_task_self(), &rx, bytes, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) return fail("Cannot allocate JIT region");
    }
    vm_prot_t current = 0, maximum = 0;
    if (vm_remap(mach_task_self(), &rw, bytes, 0, VM_FLAGS_ANYWHERE, mach_task_self(), rx,
                      false, &current, &maximum, VM_INHERIT_NONE) != KERN_SUCCESS)
        return fail("Cannot create shared writable JIT alias");
    if (vm_protect(mach_task_self(), rw, bytes, false, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS ||
        vm_protect(mach_task_self(), rx, bytes, false, VM_PROT_READ | VM_PROT_EXECUTE) != KERN_SUCCESS)
        return fail("Cannot establish W^X JIT mappings");
    // Reserve the first page for this test. TCG must use the rest of this SAME arena.
    const uint32_t code[] = {0x52800540, 0xd65f03c0}; // mov w0,#42; ret
    std::memcpy(reinterpret_cast<void*>(rw), code, sizeof(code));
    sys_dcache_flush(reinterpret_cast<void*>(rw), sizeof(code));
    sys_icache_invalidate(reinterpret_cast<void*>(rx), sizeof(code));
    detach.finish();
    const auto test = reinterpret_cast<int (*)(void)>(rx);
    if (test() != 42) return fail("Executable JIT self-test failed");
    ready.store(true, std::memory_order_release);
    return true;
}
bool AEJITArenaReady(void) { return ready.load(std::memory_order_acquire); }
bool AEIsDebuggerAttached(void) { return debuggerAttached(); }
const void* AEJITExecutableBase(void) { return AEJITArenaReady() ? reinterpret_cast<void*>(rx + vm_page_size) : nullptr; }
void* AEJITWritableBase(void) { return AEJITArenaReady() ? reinterpret_cast<void*>(rw + vm_page_size) : nullptr; }
size_t AEJITArenaSize(void) { return AEJITArenaReady() ? arenaBytes - vm_page_size : 0; }
