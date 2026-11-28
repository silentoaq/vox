#import "updater.h"

#import <CommonCrypto/CommonDigest.h>
#import <Security/Security.h>
#include <unistd.h>

#include <optional>

#include "version.h"

#import "common.h"
#import "files.h"
#import "http.h"

namespace {

NSString* const kVersion = @VOX_VERSION_STRING;
NSString* const kManifestFile = @"mac.json";
NSString* const kSignatureFile = @"mac.sig";
NSString* const kArchiveFile = @"Vox.zip";
NSString* const kStagedApp = @"new/Vox.app";
NSString* const kAttemptedFile = @"attempted";
NSString* const kRejectedFile = @"updateRejected";
constexpr size_t kBlobHeader = 8;
constexpr size_t kSignatureBytes = 64;
constexpr size_t kHashChunk = 1 << 20;

struct Release {
  NSString* version;
  NSString* url;
  double size;
  NSString* sha256;
};

UpdateStatus failed(NSString* message) { return {@"error", @"", message}; }

NSString* stagingDir() { return appDataPath(@"update"); }
NSString* staged(NSString* name) { return [stagingDir() stringByAppendingPathComponent:name]; }

NSArray<NSNumber*>* versionParts(NSString* text) {
  NSMutableArray* parts = [NSMutableArray array];
  for (NSString* part in [text componentsSeparatedByString:@"."]) [parts addObject:@(part.integerValue)];
  return parts;
}

bool newer(NSString* candidate, NSString* base) {
  NSArray* a = versionParts(candidate);
  NSArray* b = versionParts(base);
  for (NSUInteger i = 0; i < MAX(a.count, b.count); ++i) {
    const NSInteger x = i < a.count ? [a[i] integerValue] : -1;
    const NSInteger y = i < b.count ? [b[i] integerValue] : -1;
    if (x != y) return x > y;
  }
  return false;
}

bool isVersionText(NSString* text) {
  return [text rangeOfString:@"^[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?$" options:NSRegularExpressionSearch].location != NSNotFound;
}

NSString* hexOf(const unsigned char* digest, size_t size) {
  NSMutableString* out = [NSMutableString string];
  for (size_t i = 0; i < size; ++i) [out appendFormat:@"%02x", digest[i]];
  return out;
}

NSString* fileSha256(NSString* path) {
  NSFileHandle* file = [NSFileHandle fileHandleForReadingAtPath:path];
  if (!file) return nil;
  CC_SHA256_CTX context;
  CC_SHA256_Init(&context);
  for (;;) {
    NSData* chunk = [file readDataUpToLength:kHashChunk error:nil];
    if (!chunk.length) break;
    CC_SHA256_Update(&context, chunk.bytes, CC_LONG(chunk.length));
  }
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &context);
  return hexOf(digest, sizeof digest);
}

NSData* derInteger(const uint8_t* bytes, size_t size) {
  while (size > 1 && bytes[0] == 0) {
    ++bytes;
    --size;
  }
  NSMutableData* out = [NSMutableData data];
  const bool pad = bytes[0] & 0x80;
  const uint8_t header[] = {0x02, uint8_t(size + (pad ? 1 : 0))};
  [out appendBytes:header length:sizeof header];
  if (pad) [out appendBytes:"\0" length:1];
  [out appendBytes:bytes length:size];
  return out;
}

NSData* derSignature(NSData* raw) {
  const size_t half = raw.length / 2;
  const auto* bytes = static_cast<const uint8_t*>(raw.bytes);
  NSMutableData* body = [NSMutableData dataWithData:derInteger(bytes, half)];
  [body appendData:derInteger(bytes + half, half)];
  NSMutableData* out = [NSMutableData data];
  const uint8_t header[] = {0x30, uint8_t(body.length)};
  [out appendBytes:header length:sizeof header];
  [out appendData:body];
  return out;
}

bool signatureValid(NSData* message, NSData* signature) {
  if (signature.length != kSignatureBytes) return false;
  NSData* blob = [[NSData alloc] initWithBase64EncodedString:@VOX_UPDATE_KEY options:0];
  if (blob.length <= kBlobHeader) return false;
  NSMutableData* point = [NSMutableData dataWithBytes:"\x04" length:1];
  [point appendData:[blob subdataWithRange:NSMakeRange(kBlobHeader, blob.length - kBlobHeader)]];
  NSDictionary* attributes = @{
    (id)kSecAttrKeyType : (id)kSecAttrKeyTypeECSECPrimeRandom,
    (id)kSecAttrKeyClass : (id)kSecAttrKeyClassPublic,
  };
  SecKeyRef key = SecKeyCreateWithData((__bridge CFDataRef)point, (__bridge CFDictionaryRef)attributes, nullptr);
  if (!key) return false;
  const bool valid = SecKeyVerifySignature(key, kSecKeyAlgorithmECDSASignatureMessageX962SHA256, (__bridge CFDataRef)message,
                                           (__bridge CFDataRef)derSignature(signature), nullptr);
  CFRelease(key);
  return valid;
}

std::optional<Release> releaseOf(NSData* manifest, NSData* signature) {
  if (!signatureValid(manifest, signature)) return std::nullopt;
  NSDictionary* parsed = [NSJSONSerialization JSONObjectWithData:manifest options:0 error:nil];
  NSDictionary* archive = [parsed isKindOfClass:NSDictionary.class] ? parsed[@"archive"] : nil;
  if (![archive isKindOfClass:NSDictionary.class]) return std::nullopt;
  Release release{parsed[@"version"], archive[@"url"], [archive[@"size"] doubleValue], archive[@"sha256"]};
  if (![release.version isKindOfClass:NSString.class] || !isVersionText(release.version) || ![release.url isKindOfClass:NSString.class] ||
      ![release.sha256 isKindOfClass:NSString.class])
    return std::nullopt;
  return release;
}

std::optional<Release> stagedRelease() {
  NSData* manifest = [NSData dataWithContentsOfFile:staged(kManifestFile)];
  NSData* signature = [NSData dataWithContentsOfFile:staged(kSignatureFile)];
  if (!manifest || !signature) return std::nullopt;
  return releaseOf(manifest, signature);
}

bool stagedArchiveValid(const Release& release) {
  return [fileSha256(staged(kArchiveFile)) isEqualToString:release.sha256] &&
         [NSFileManager.defaultManager fileExistsAtPath:staged(kStagedApp)];
}

void removeStaging() { [NSFileManager.defaultManager removeItemAtPath:stagingDir() error:nil]; }

NSString* bundlePath() { return NSBundle.mainBundle.bundlePath; }

bool translocated() { return [bundlePath() rangeOfString:@"/AppTranslocation/"].location != NSNotFound; }

bool replaceable() {
  return !translocated() && [NSFileManager.defaultManager isWritableFileAtPath:bundlePath().stringByDeletingLastPathComponent];
}

NSData* download(NSString* url, NSString** error) {
  HttpResult result = httpFetchNow(httpRequest(url));
  if (result.error) *error = result.error;
  else if (result.status != 200) *error = [NSString stringWithFormat:@"伺服器回 %ld", long(result.status)];
  else return result.body;
  return nil;
}

UpdateStatus check() {
  NSString* error = nil;
  NSString* base = @VOX_UPDATE_URL;
  NSData* manifest = download([base stringByAppendingFormat:@"/%@", kManifestFile], &error);
  if (!manifest) return failed([@"檢查更新失敗：" stringByAppendingString:error]);
  NSData* signature = download([base stringByAppendingFormat:@"/%@", kSignatureFile], &error);
  if (!signature) return failed([@"檢查更新失敗：" stringByAppendingString:error]);
  auto release = releaseOf(manifest, signature);
  if (!release) return failed(@"更新資訊的簽章或版本號不對，這次不更新");
  if (!newer(release->version, kVersion)) {
    removeStaging();
    return {};
  }
  if ([readTextFile(appDataPath(kRejectedFile)) isEqualToString:release->version]) return {};
  if (auto current = stagedRelease(); current && [current->version isEqualToString:release->version] && stagedArchiveValid(*current))
    return {@"ready", release->version, @""};

  removeStaging();
  NSFileManager* files = NSFileManager.defaultManager;
  [files createDirectoryAtPath:stagingDir() withIntermediateDirectories:YES attributes:nil error:nil];
  NSString* partial = [staged(kArchiveFile) stringByAppendingString:@".partial"];
  if (NSString* failure = httpDownloadNow(release->url, partial)) return failed([@"下載新版失敗：" stringByAppendingString:failure]);
  NSDictionary* attributes = [files attributesOfItemAtPath:partial error:nil];
  if (double([attributes fileSize]) != release->size || ![fileSha256(partial) isEqualToString:release->sha256])
    return failed(@"下載的新版跟更新資訊對不上");
  if (![files moveItemAtPath:partial toPath:staged(kArchiveFile) error:nil]) return failed(@"新版存不起來");
  NSString* unpacked = staged(@"new");
  if (runTool(@"/usr/bin/ditto", @[ @"-x", @"-k", staged(kArchiveFile), unpacked ]) != 0 ||
      ![files fileExistsAtPath:staged(kStagedApp)])
    return failed(@"新版解壓縮失敗");
  writeDataFile(staged(kSignatureFile), signature);
  writeDataFile(staged(kManifestFile), manifest);
  return {@"ready", release->version, @""};
}

}

bool launchStagedUpdate() {
  auto release = stagedRelease();
  if (!release) return false;
  if (!newer(release->version, kVersion)) {
    removeStaging();
    return false;
  }
  if ([readTextFile(staged(kAttemptedFile)) isEqualToString:release->version] || !stagedArchiveValid(*release) || !replaceable()) return false;
  writeTextFile(staged(kAttemptedFile), release->version);
  NSString* script = @"while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; "
                     @"rm -rf \"$2.old\" && mv \"$2\" \"$2.old\" && { mv \"$3\" \"$2\" || mv \"$2.old\" \"$2\"; }; "
                     @"rm -rf \"$2.old\"; open \"$2\"";
  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:@"/bin/sh"];
  task.arguments = @[ @"-c", script, @"vox-update", [@(getpid()) stringValue], bundlePath(), staged(kStagedApp) ];
  return [task launchAndReturnError:nil];
}

Updater::Updater(Notify notify) : notify_(std::move(notify)) {}

void Updater::started() {
  if (checked_) return;
  checked_ = true;
  if (auto current = stagedRelease(); current && [readTextFile(staged(kAttemptedFile)) isEqualToString:current->version]) {
    writeTextFile(appDataPath(kRejectedFile), current->version);
    removeStaging();
    notify_({@"rejected", current->version, @""});
  }
  if (translocated()) {
    notify_(failed(@"先把 Vox 拖到「應用程式」資料夾再打開，才能自動更新"));
    return;
  }
  if (!replaceable()) return;
  Notify notify = notify_;
  inBackground(^{
    @autoreleasepool {
      UpdateStatus status = check();
      if (status.state.length) onMain(^{
        notify(status);
      });
    }
  });
}
