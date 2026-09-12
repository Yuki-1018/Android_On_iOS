#import "RuntimeMetrics.h"
#import <QuartzCore/QuartzCore.h>
#include <atomic>
#include <mach/mach.h>
#include "QEMUBridge/NativeBridge.h"
@implementation AERuntimeMetrics {
    std::atomic<uint64_t> _frames, _presented, _bytes, _audioDrops;
    uint64_t _previousFrames, _previousPresented, _previousBytes;
    CFTimeInterval _previousTime;
}
- (instancetype)init {
    if ((self = [super init])) { _previousTime = CACurrentMediaTime(); }
    return self;
}
- (void)receivedFrameBytes:(uint64_t)bytes { _frames.fetch_add(1); _bytes.fetch_add(bytes); }
- (void)presentedFrame { _presented.fetch_add(1); }
- (void)droppedAudio { _audioDrops.fetch_add(1); }
- (NSDictionary<NSString *, NSNumber *> *)snapshot {
    // Called by the UI sampler, once a second; hot paths only touch atomics.
    CFTimeInterval now = CACurrentMediaTime(), elapsed = MAX(now - _previousTime, 0.001);
    uint64_t frames = _frames.load(), presented = _presented.load(), bytes = _bytes.load();
    task_vm_info_data_t vm = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    uint64_t footprint = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count) == KERN_SUCCESS ? vm.phys_footprint : 0;
    NSDictionary *result = @{@"totalGuestUpdates": @(frames), @"guestUpdatesPerSecond": @((frames - _previousFrames) / elapsed),
        @"presentationsPerSecond": @((presented - _previousPresented) / elapsed),
        @"copyMiBPerSecond": @((bytes - _previousBytes) / elapsed / 1048576.0),
        @"footprintMiB": @(footprint / 1048576.0), @"audioDrops": @(_audioDrops.load()),
        @"availableMemoryMiB": @(AEAvailableMemory() / 1048576.0),
        @"increasedMemoryLimit": @(AEHasIncreasedMemoryLimit()),
        @"thermalState": @([NSProcessInfo processInfo].thermalState)};
    _previousFrames = frames; _previousPresented = presented; _previousBytes = bytes; _previousTime = now;
    return result;
}
@end
