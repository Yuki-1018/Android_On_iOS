#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface AEAudioOutput : NSObject
- (BOOL)startWithSampleRate:(double)sampleRate error:(NSError **)error;
- (void)stop;
- (BOOL)writeStereoPCM:(const int16_t *)samples frames:(size_t)frames;
- (uint64_t)underruns;
@end
NS_ASSUME_NONNULL_END
