#import "ADBClient.h"
#import "ADBKey.h"
#include "Client.hpp"
#include <atomic>
#include <dlfcn.h>
#include <memory>
#include <thread>
#include <stdexcept>

@implementation AEADBClient {
    dispatch_queue_t _queue;
    std::unique_ptr<emu::adb::Client> _client;
    AEADBKey *_key;
    std::atomic<bool> _cancelled;
    bool (*_connected)(void);
    void (*_disconnect)(void);
    size_t (*_read)(uint8_t *,size_t);
    size_t (*_write)(const uint8_t *,size_t);
    BOOL _busy, _bootCompleted;
    double _transferProgress;
    NSString *_statusText, *_outputText;
}
- (instancetype)initWithEngineHandle:(void *)handle {
    if ((self=[super init])) {
        _queue=dispatch_queue_create("org.androidemu.adb",DISPATCH_QUEUE_SERIAL);
        _key=[AEADBKey new]; _statusText=@"adbd待機中"; _outputText=@"";
        _disconnect=reinterpret_cast<decltype(_disconnect)>(dlsym(handle,"android51_adb_disconnect"));
        _connected=reinterpret_cast<decltype(_connected)>(dlsym(handle,"android51_adb_connected"));
        _read=reinterpret_cast<decltype(_read)>(dlsym(handle,"android51_adb_read"));
        _write=reinterpret_cast<decltype(_write)>(dlsym(handle,"android51_adb_write"));
    }
    return self;
}
- (BOOL)busy { return _busy; }
- (BOOL)bootCompleted { return _bootCompleted; }
- (double)transferProgress { return _transferProgress; }
- (NSString *)statusText { return _statusText; }
- (NSString *)outputText { return _outputText; }
- (void)connect {
    if (!_connected || !_disconnect || !_read || !_write) throw std::runtime_error("ADB transport ABI is unavailable");
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(120);
    while (!_connected()) {
        if (_cancelled.load()) throw std::runtime_error("ADB cancelled");
        if (std::chrono::steady_clock::now()>=end) throw std::runtime_error("adbd is not ready; check Android boot log");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (_client) return;
    auto connected=_connected; auto reader=_read; auto writer=_write; auto cancel=&_cancelled; AEADBKey *key=_key;
    emu::adb::Transport transport = {
        [connected]{return connected();}, [cancel]{return cancel->load();},
        [reader](std::span<uint8_t> bytes){return reader(bytes.data(),bytes.size());},
        [writer](std::span<const uint8_t> bytes){return writer(bytes.data(),bytes.size());},
        [key](std::span<const uint8_t> token) {
            NSError *error=nil; NSData *result=[key signToken:[NSData dataWithBytes:token.data() length:token.size()] error:&error];
            if (!result) throw std::runtime_error(error.localizedDescription.UTF8String ?: "ADB signing failed");
            const auto *bytes=static_cast<const uint8_t *>(result.bytes); return std::vector<uint8_t>(bytes,bytes+result.length);
        }, [key] {
            NSError *error=nil; NSData *result=[key publicKeyWithError:&error];
            if (!result) throw std::runtime_error(error.localizedDescription.UTF8String ?: "ADB public key failed");
            const auto *bytes=static_cast<const uint8_t *>(result.bytes); return std::vector<uint8_t>(bytes,bytes+result.length);
        }};
    _client=std::make_unique<emu::adb::Client>(std::move(transport)); _client->connect();
}
- (void)perform:(NSString *)label action:(NSString *(^)(void))action {
    [self perform:label publishOutput:YES action:action];
}
- (void)perform:(NSString *)label publishOutput:(BOOL)publishOutput action:(NSString *(^)(void))action {
    NSAssert([NSThread isMainThread],@"ADB operations must originate on UI thread");
    if (_busy || _cancelled.load()) return;
    _busy=YES; _statusText=label; _transferProgress=0;
    dispatch_async(_queue, ^{
        @autoreleasepool {
            NSString *output=nil; BOOL success=YES;
            try { [self connect]; output=action(); }
            catch (const std::exception& e) { success=NO; self->_client.reset(); if (self->_disconnect) self->_disconnect(); output=@(e.what()); }
            dispatch_async(dispatch_get_main_queue(), ^{
                if (publishOutput || !success) self->_outputText=output ?: @"";
                self->_busy=NO;
                self->_statusText=success ? @"ADB接続済み" : [@"ADB: " stringByAppendingString:output ?: @"接続失敗"];
            });
        }
    });
}
- (void)checkBoot {
    // Background polling must not monopolize the queue while adbd is absent.
    if (!_connected || !_connected()) return;
    [self perform:@"Androidの起動を確認中" publishOutput:NO action:^NSString *{
        auto output=self->_client->shell("getprop sys.boot_completed",4096);
        NSString *text=[[NSString alloc] initWithBytes:output.data() length:output.size() encoding:NSUTF8StringEncoding] ?: @"";
        BOOL complete=[[text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] isEqualToString:@"1"];
        dispatch_async(dispatch_get_main_queue(), ^{ self->_bootCompleted=complete; });
        return complete ? @"Androidの起動完了を確認しました" : @"Androidはまだ起動処理中です";
    }];
}
- (void)runShell:(NSString *)command {
    [self perform:@"コマンドを実行中" action:^NSString *{
        auto output=self->_client->shell(command.UTF8String);
        return [[NSString alloc] initWithBytes:output.data() length:output.size() encoding:NSUTF8StringEncoding] ?: @"出力をUTF-8として表示できません";
    }];
}
- (void)installAPK:(NSURL *)url {
    [self perform:@"APKを転送・インストール中" action:^NSString *{
        BOOL scoped=[url startAccessingSecurityScopedResource];
        @try {
            NSString *name=[[NSUUID.UUID.UUIDString lowercaseString] stringByAppendingString:@".apk"];
            std::string destination="/data/local/tmp/"+std::string(name.UTF8String);
            std::string output;
            try {
                auto last=std::make_shared<uint64_t>(0);
                self->_client->push(url.fileSystemRepresentation,destination,[self,last](uint64_t sent,uint64_t total){
                    if (sent!=total && sent-*last<512*1024) return;
                    *last=sent; double progress=double(sent)/double(total);
                    dispatch_async(dispatch_get_main_queue(), ^{ self->_transferProgress=progress; });
                });
                output=self->_client->shell(emu::adb::installCommand(name.UTF8String,true)+"; printf '\\n__ANDROIDEMU_EXIT_%d\\n' $?", 1024 * 1024, std::chrono::seconds(600));
            } catch (...) {
                try { self->_client->shell("rm -f "+emu::adb::shellQuote(destination),4096, std::chrono::seconds(20)); } catch (...) {}
                throw;
            }
            // Installation result is authoritative. A temporary-file cleanup
            // failure must not report a successfully installed APK as failed.
            try { self->_client->shell("rm -f "+emu::adb::shellQuote(destination),4096, std::chrono::seconds(20)); }
            catch (...) { self->_client.reset(); if (self->_disconnect) self->_disconnect(); }
            if (output.find("__ANDROIDEMU_EXIT_0")==std::string::npos) throw std::runtime_error(output);
            return [[NSString alloc] initWithBytes:output.data() length:output.size() encoding:NSUTF8StringEncoding] ?: @"インストールが完了しました";
        } @finally { if (scoped) [url stopAccessingSecurityScopedResource]; }
    }];
}
- (void)cancel { _cancelled.store(true); }
@end
