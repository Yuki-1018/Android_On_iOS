#pragma once
#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface AEADBKey : NSObject
- (nullable NSData *)signToken:(NSData *)token error:(NSError **)error;
- (nullable NSData *)publicKeyWithError:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
