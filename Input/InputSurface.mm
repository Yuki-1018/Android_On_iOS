#import "InputSurface.h"
#include "Touch.hpp"
#include "Keyboard.hpp"
#include <bitset>
#include <array>

@implementation AEInputSurface {
    emu::TouchInput _input;
    uint32_t _guestWidth, _guestHeight;
    NSMutableSet<UITouch *> *_activeTouches;
    NSTimer *_retryTimer;
    BOOL _resetPending;
    std::bitset<768> _heldKeys;
}
- (instancetype)initWithGuestWidth:(uint32_t)width height:(uint32_t)height {
    if ((self = [super initWithFrame:CGRectZero])) {
        _guestWidth = width; _guestHeight = height;
        _activeTouches = [NSMutableSet setWithCapacity:10];
        self.multipleTouchEnabled = YES;
        self.backgroundColor = UIColor.clearColor;
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationWillResignActive:) name:UIApplicationWillResignActiveNotification object:nil];
    }
    return self;
}
- (BOOL)canBecomeFirstResponder { return YES; }
- (void)applicationWillResignActive:(NSNotification *)notification { (void)notification; [self cancelTouches]; }
- (void)cancelTouches {
    [_activeTouches removeAllObjects];
    _resetPending = ![self resetInput];
    if (_resetPending && !_retryTimer) {
        __weak AEInputSurface *weakSelf = self;
        _retryTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/60.0 repeats:YES block:^(NSTimer *timer) {
            AEInputSurface *surface = weakSelf;
            if (!surface) { [timer invalidate]; return; }
            if ([surface resetInput]) {
                surface->_resetPending = NO;
                [timer invalidate]; surface->_retryTimer = nil;
            }
        }];
    }
}
- (void)deliver:(NSSet<UITouch *> *)touches phase:(emu::TouchPhase)phase {
    if (_resetPending) return;
    for (UITouch *touch in touches) {
        BOOL down = phase == emu::TouchPhase::down;
        if (!down && ![_activeTouches containsObject:touch]) continue;
        if (down && _activeTouches.count == 10) continue;
        CGPoint location = [touch locationInView:self];
        auto position = emu::guestPoint({location.x, location.y}, self.bounds.size.width, self.bounds.size.height,
                                       int(_guestWidth), int(_guestHeight), emu::Rotation::upright, !down);
        if (!position) continue;
        uint64_t identity = reinterpret_cast<uint64_t>((__bridge void *)touch);
        if (!_input.touch(identity, phase, int32_t(position->x), int32_t(position->y))) {
            [self cancelTouches]; return;
        }
        if (down) [_activeTouches addObject:touch];
        if (phase == emu::TouchPhase::up || phase == emu::TouchPhase::cancel) [_activeTouches removeObject:touch];
    }
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event; [self becomeFirstResponder]; [self deliver:touches phase:emu::TouchPhase::down];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self deliver:touches phase:emu::TouchPhase::move]; }
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self deliver:touches phase:emu::TouchPhase::up]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { (void)event; [self deliver:touches phase:emu::TouchPhase::cancel]; }
- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (!self.window) [self cancelTouches];
}
- (BOOL)resetInput {
    BOOL complete = _input.cancelAll();
    for (size_t code = 1; code < _heldKeys.size(); ++code) {
        if (_heldKeys[code]) {
            if (_input.key(static_cast<emu::HardwareKey>(code), 0)) _heldKeys.reset(code);
            else complete = NO;
        }
    }
    return complete;
}
- (BOOL)sendHardwareKey:(uint16_t)code value:(int32_t)value {
    if (_resetPending || code >= _heldKeys.size() || value < 0 || value > 2) return NO;
    BOOL supported = NO;
    switch (code) {
        case 102: case 114: case 115: case 116: case 158: case 172: case 139: case 217: supported = YES; break;
        default: break;
    }
    if (!supported) return NO;
    if (!_input.key(static_cast<emu::HardwareKey>(code), value)) { [self cancelTouches]; return NO; }
    _heldKeys.set(code, value != 0);
    return YES;
}
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    NSMutableSet<UIPress *> *unhandled = [NSMutableSet set];
    for (UIPress *press in presses) {
        uint16_t code = press.key ? emu::linuxKeyForHID((uint16_t)press.key.keyCode) : 0;
        if (!code) [unhandled addObject:press];
        else if (![self sendHardwareKey:code value:_heldKeys[code] ? 2 : 1]) [unhandled addObject:press];
    }
    if (unhandled.count) [super pressesBegan:unhandled withEvent:event];
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    NSMutableSet<UIPress *> *unhandled = [NSMutableSet set];
    for (UIPress *press in presses) {
        uint16_t code = press.key ? emu::linuxKeyForHID((uint16_t)press.key.keyCode) : 0;
        if (!code) [unhandled addObject:press]; else [self sendHardwareKey:code value:0];
    }
    if (unhandled.count) [super pressesEnded:unhandled withEvent:event];
}
- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    [self pressesEnded:presses withEvent:event];
}
- (size_t)readEvents:(AEInputEvent *)events capacity:(size_t)capacity {
    if (!events) return 0;
    std::array<emu::InputEvent, 128> scratch;
    size_t total = 0;
    while (total < capacity) {
        const size_t count = _input.read(std::span(scratch.data(), std::min(capacity - total, scratch.size())));
        for (size_t i = 0; i < count; ++i) events[total+i] = {scratch[i].type, scratch[i].code, scratch[i].value};
        total += count;
        if (!count) break;
    }
    return total;
}
- (void)dealloc {
    [_retryTimer invalidate];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}
@end
