#import <UIKit/UIKit.h>
NS_ASSUME_NONNULL_BEGIN
typedef struct { uint16_t type, code; int32_t value; } AEInputEvent;
@interface AEInputSurface : UIView
- (instancetype)initWithGuestWidth:(uint32_t)width height:(uint32_t)height;
// Call from the one QEMU input consumer thread. Apply events under QEMU's BQL.
- (size_t)readEvents:(AEInputEvent *)events capacity:(size_t)capacity;
// Linux evdev codes: release=0, press=1, repeat=2. Invoke on the UIKit thread.
- (BOOL)sendHardwareKey:(uint16_t)code value:(int32_t)value;
- (void)cancelTouches;
@end
NS_ASSUME_NONNULL_END
