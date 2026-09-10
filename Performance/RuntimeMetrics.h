#pragma once
#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface AERuntimeMetrics : NSObject
- (void)receivedFrameBytes:(uint64_t)bytes;
- (void)presentedFrame;
- (void)droppedAudio;
- (NSDictionary<NSString *, NSNumber *> *)snapshot;
@end
NS_ASSUME_NONNULL_END
