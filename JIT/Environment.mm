/* TXM detection adapted from StikDebug/StikJIT ProcessInfo+TXM.swift,
 * revision 3623e725876f76aecb0520582ad6194bacb15d39. Ported to Objective-C++,
 * dynamically resolves iOS SPI and also inspects SPTM.
 * This Source Code Form is subject to the Mozilla Public License, v. 2.0.
 * See ThirdParty/StikJITProtocol/LICENSE.
 */
#include "QEMUBridge/NativeBridge.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <unistd.h>
#include <mutex>

namespace {
template<class T> T symbol(void* handle, const char* name) { return reinterpret_cast<T>(dlsym(handle, name)); }
// iOS SPI stays isolated here; a missing symbol returns unknown/false.
int memoryMapFeature(CFStringRef feature) {
    static void* io = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_LAZY | RTLD_LOCAL);
    if (!io) return -1;
    auto fromPath = symbol<uint32_t (*)(uint32_t, const char*)>(io, "IORegistryEntryFromPath");
    auto property = symbol<CFTypeRef (*)(uint32_t, CFStringRef, CFAllocatorRef, uint32_t)>(io, "IORegistryEntryCreateCFProperty");
    auto release = symbol<int (*)(uint32_t)>(io, "IOObjectRelease");
    if (!fromPath || !property || !release) return -1;
    const auto entry = fromPath(0, "IODeviceTree:/chosen/memory-map");
    if (!entry) return -1;
    CFTypeRef keys = property(entry, CFSTR("IORegistryEntryPropertyKeys"), kCFAllocatorDefault, 0);
    release(entry);
    if (!keys) return -1;
    int result = -1;
    if (CFGetTypeID(keys) == CFArrayGetTypeID()) {
        auto array = static_cast<CFArrayRef>(keys);
        result = CFArrayContainsValue(array, CFRangeMake(0, CFArrayGetCount(array)), feature) ? 1 : 0;
    }
    CFRelease(keys); return result;
}
}
bool AEHasGetTaskAllow(void) {
    static void* security = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY | RTLD_LOCAL);
    if (!security) return false;
    auto create = symbol<CFTypeRef (*)(CFAllocatorRef)>(security, "SecTaskCreateFromSelf");
    auto copy = symbol<CFTypeRef (*)(CFTypeRef, CFStringRef, CFErrorRef*)>(security, "SecTaskCopyValueForEntitlement");
    if (!create || !copy) return false;
    auto task = create(kCFAllocatorDefault);
    if (!task) return false;
    auto value = copy(task, CFSTR("get-task-allow"), nullptr);
    const bool allowed = value && CFEqual(value, kCFBooleanTrue);
    if (value) CFRelease(value);
    CFRelease(task); return allowed;
}
bool AEIsDebugged(void) {
    auto csops = symbol<int (*)(pid_t, unsigned int, void*, size_t)>(RTLD_DEFAULT, "csops");
    uint32_t flags = 0;
    return csops && csops(getpid(), 0, &flags, sizeof(flags)) == 0 && (flags & 0x10000000U) != 0;
}
int AETXMPresence(void) { return memoryMapFeature(CFSTR("TXM")); }
int AESPTMPresence(void) { return memoryMapFeature(CFSTR("SPTM")); }
