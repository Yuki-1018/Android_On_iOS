#pragma once
#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface AEADBClient : NSObject
@property(nonatomic, readonly) BOOL busy;
@property(nonatomic, readonly) BOOL bootCompleted;
@property(nonatomic, readonly) double transferProgress;
@property(nonatomic, readonly) NSString *statusText;
@property(nonatomic, readonly) NSString *outputText;
- (instancetype)initWithEngineHandle:(void *)handle;
- (void)checkBoot NS_SWIFT_NAME(checkBoot());
- (void)runShell:(NSString *)command NS_SWIFT_NAME(runShell(_:));
- (void)installAPK:(NSURL *)url NS_SWIFT_NAME(installAPK(_:));
- (void)cancel;
@end
NS_ASSUME_NONNULL_END
