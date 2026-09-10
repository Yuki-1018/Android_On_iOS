#import "AudioOutput.h"
#import <AVFoundation/AVFoundation.h>
#include "PCMRing.hpp"
#include <memory>
#include <array>
#include <mutex>

@implementation AEAudioOutput {
    std::shared_ptr<emu::PCMRing> _ring;
    std::shared_ptr<emu::PCMRing> _writeRing;
    std::mutex _producerLock;
    uint64_t _previousUnderruns;
    AVAudioEngine *_engine;
    AVAudioSourceNode *_source;
    id _interruption;
    BOOL _wanted, _sessionActive;
    double _sampleRate;
}
- (instancetype)init {
    if ((self = [super init])) { _ring = std::make_shared<emu::PCMRing>(); }
    return self;
}
- (BOOL)startWithSampleRate:(double)rate error:(NSError **)error {
    if (rate != 44100 && rate != 48000) {
        if (error) *error = [NSError errorWithDomain:@"AndroidEmu.Audio" code:1 userInfo:@{NSLocalizedDescriptionKey: @"Only 44.1/48 kHz stereo PCM is supported"}];
        return NO;
    }
    [self stop];
    AVAudioSession *session = [AVAudioSession sharedInstance];
    if (![session setCategory:AVAudioSessionCategoryPlayback error:error] || ![session setActive:YES error:error]) return NO;
    _sessionActive = YES; _sampleRate = rate;
    _engine = [AVAudioEngine new];
    AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:rate channels:2];
    if (_ring) _previousUnderruns += _ring->underruns.load(std::memory_order_relaxed);
    _ring = std::make_shared<emu::PCMRing>();
    auto ring = _ring;
    _source = [[AVAudioSourceNode alloc] initWithFormat:format renderBlock:^OSStatus(BOOL *silent, const AudioTimeStamp *timestamp, AVAudioFrameCount frames, AudioBufferList *data) {
        (void)timestamp;
        if (data->mNumberBuffers != 2) return kAudio_ParamError;
        auto *left = static_cast<float *>(data->mBuffers[0].mData);
        auto *right = static_cast<float *>(data->mBuffers[1].mData);
        std::array<emu::StereoSample, 512> scratch;
        for (size_t offset = 0; offset < frames;) {
            const size_t count = std::min<size_t>(frames - offset, scratch.size());
            ring->render(std::span(scratch.data(), count));
            for (size_t i = 0; i < count; ++i) {
                left[offset+i] = scratch[i].left / 32768.0f;
                right[offset+i] = scratch[i].right / 32768.0f;
            }
            offset += count;
        }
        *silent = NO; return noErr;
    }];
    [_engine attachNode:_source];
    [_engine connect:_source to:_engine.mainMixerNode format:format];
    [_engine prepare];
    if (![_engine startAndReturnError:error]) { [self stop]; return NO; }
    { std::lock_guard guard(_producerLock); _writeRing = ring; }
    _wanted = YES;
    __weak AEAudioOutput *weakSelf = self;
    _interruption = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionInterruptionNotification object:session queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *note) {
        AEAudioOutput *strongSelf = weakSelf;
        if (!strongSelf) return;
        const auto type = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
        if (type == AVAudioSessionInterruptionTypeBegan) [strongSelf->_engine pause];
        else if (strongSelf->_wanted && ([note.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue] & AVAudioSessionInterruptionOptionShouldResume)) {
            NSError *resumeError = nil;
            [strongSelf startWithSampleRate:strongSelf->_sampleRate error:&resumeError];
        }
    }];
    return YES;
}
- (void)stop {
    { std::lock_guard guard(_producerLock); _writeRing.reset(); }
    _wanted = NO;
    if (_interruption) { [[NSNotificationCenter defaultCenter] removeObserver:_interruption]; _interruption = nil; }
    [_engine stop]; _engine = nil; _source = nil;
    if (_sessionActive) {
        [[AVAudioSession sharedInstance] setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:NULL];
        _sessionActive = NO;
    }
}
- (BOOL)writeStereoPCM:(const int16_t *)samples frames:(size_t)frames {
    if (!samples || frames > 4096) return NO;
    std::shared_ptr<emu::PCMRing> ring;
    { std::lock_guard guard(_producerLock); ring = _writeRing; }
    if (!ring) return NO;
    // Avoid aliasing int16_t memory as a struct object; bounded stack conversion.
    std::array<emu::StereoSample, 4096> converted;
    for (size_t i = 0; i < frames; ++i) converted[i] = {samples[2*i], samples[2*i+1]};
    return ring->write(std::span(converted.data(), frames));
}
- (uint64_t)underruns { return _previousUnderruns + (_ring ? _ring->underruns.load(std::memory_order_relaxed) : 0); }
- (void)dealloc { [self stop]; }
@end
