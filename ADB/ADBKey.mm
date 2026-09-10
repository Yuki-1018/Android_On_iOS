#import "ADBKey.h"
#import <Security/Security.h>
#include "RSAPublicKey.hpp"
#include <span>
#include <stdexcept>
namespace {
std::span<const uint8_t> derValue(std::span<const uint8_t>& bytes, uint8_t tag) {
    if (bytes.size()<2 || bytes[0]!=tag) throw std::runtime_error("Invalid RSA key encoding");
    size_t size=bytes[1], header=2;
    if (size&0x80) {
        size_t count=size&0x7f; size=0;
        if (!count || count>4 || bytes.size()<2+count) throw std::runtime_error("Invalid DER length");
        for (size_t i=0;i<count;++i) size=(size<<8)|bytes[header++];
    }
    if (size>bytes.size()-header) throw std::runtime_error("Truncated RSA key");
    auto result=bytes.subspan(header,size); bytes=bytes.subspan(header+size); return result;
}
}
@implementation AEADBKey {
    SecKeyRef _key;
}
- (BOOL)loadKey:(NSError **)error {
    if (_key) return YES;
    NSData *tag = [@"org.androidemu.adb.rsa2048" dataUsingEncoding:NSUTF8StringEncoding];
    NSDictionary *query = @{(__bridge id)kSecClass: (__bridge id)kSecClassKey,
        (__bridge id)kSecAttrApplicationTag: tag, (__bridge id)kSecAttrKeyType: (__bridge id)kSecAttrKeyTypeRSA,
        (__bridge id)kSecReturnRef: @YES};
    CFTypeRef found = NULL;
    OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &found);
    if (status == errSecSuccess) { _key = (SecKeyRef)found; return YES; }
    if (status != errSecItemNotFound) {
        if (error) *error = [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil]; return NO;
    }
    CFErrorRef failure = NULL;
    NSDictionary *attributes = @{(__bridge id)kSecAttrKeyType: (__bridge id)kSecAttrKeyTypeRSA,
        (__bridge id)kSecAttrKeySizeInBits: @2048,
        (__bridge id)kSecPrivateKeyAttrs: @{(__bridge id)kSecAttrIsPermanent: @YES,
            (__bridge id)kSecAttrApplicationTag: tag, (__bridge id)kSecAttrAccessible: (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly}};
    _key = SecKeyCreateRandomKey((__bridge CFDictionaryRef)attributes, &failure);
    if (!_key) { NSError *e = CFBridgingRelease(failure); if (error) *error = e; return NO; }
    return YES;
}
- (NSData *)signToken:(NSData *)token error:(NSError **)error {
    if (token.length != 20 || ![self loadKey:error]) return nil;
    CFErrorRef failure = NULL;
    CFDataRef signature = SecKeyCreateSignature(_key, kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1, (__bridge CFDataRef)token, &failure);
    if (!signature) { NSError *e = CFBridgingRelease(failure); if (error) *error = e; return nil; }
    return CFBridgingRelease(signature);
}
- (NSData *)publicKeyWithError:(NSError **)error {
    if (![self loadKey:error]) return nil;
    SecKeyRef publicKey = SecKeyCopyPublicKey(_key);
    if (!publicKey) return nil;
    CFErrorRef failure = NULL;
    NSData *encoded = CFBridgingRelease(SecKeyCopyExternalRepresentation(publicKey, &failure));
    CFRelease(publicKey);
    if (!encoded) { NSError *e = CFBridgingRelease(failure); if (error) *error = e; return nil; }
    try {
        auto bytes=std::span(static_cast<const uint8_t *>(encoded.bytes),encoded.length);
        auto sequence=derValue(bytes,0x30);
        auto modulus=derValue(sequence,0x02), exponent=derValue(sequence,0x02);
        if (!bytes.empty() || !sequence.empty() || exponent.size()>4) throw std::runtime_error("Invalid RSA public key");
        if (modulus.size()==257 && modulus.front()==0) modulus=modulus.subspan(1);
        uint32_t e=0; for (auto byte:exponent) e=(e<<8)|byte;
        auto key=emu::adb::androidPublicKey(modulus,e);
        NSData *wire=[NSData dataWithBytes:key.data() length:key.size()];
        NSString *line=[[wire base64EncodedStringWithOptions:0] stringByAppendingString:@" androidemu@ios"];
        NSMutableData *result=[[line dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
        const uint8_t zero=0; [result appendBytes:&zero length:1]; return result;
    } catch (const std::exception& e) {
        if (error) *error=[NSError errorWithDomain:@"AndroidEmu.ADB" code:1 userInfo:@{NSLocalizedDescriptionKey:@(e.what())}]; return nil;
    }
}
- (void)dealloc { if (_key) CFRelease(_key); }
@end
