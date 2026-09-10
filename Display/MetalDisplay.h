#import <MetalKit/MetalKit.h>
@class AERuntimeMetrics;
NS_ASSUME_NONNULL_BEGIN
// A QEMU display listener must submit BGRA updates before using this view.
@interface AEMetalDisplay : MTKView
@property(nonatomic, strong, nullable) AERuntimeMetrics *runtimeMetrics;
- (nullable instancetype)initWithGuestWidth:(uint32_t)width height:(uint32_t)height error:(NSError **)error;
- (BOOL)submitPixels:(const uint8_t *)pixels length:(size_t)length stride:(size_t)stride
                   x:(uint32_t)x y:(uint32_t)y width:(uint32_t)width height:(uint32_t)height;
- (uint64_t)coalescedUpdates;
@end
NS_ASSUME_NONNULL_END
