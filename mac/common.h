#pragma once
#import <Foundation/Foundation.h>

#include <stdexcept>
#include <string>

struct HostError : std::runtime_error {
  explicit HostError(NSString* message) : std::runtime_error(message.UTF8String) {}
};

inline NSString* ns(const std::wstring& text) {
  return [[NSString alloc] initWithBytes:text.data() length:text.size() * sizeof(wchar_t) encoding:NSUTF32LittleEndianStringEncoding];
}

inline NSString* ns(const std::string& text) { return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding]; }

inline std::wstring toWide(NSString* text) {
  NSData* data = [text dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
  return std::wstring(static_cast<const wchar_t*>(data.bytes), data.length / sizeof(wchar_t));
}

inline void onMain(dispatch_block_t block) { dispatch_async(dispatch_get_main_queue(), block); }

inline void inBackground(dispatch_block_t block) { dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), block); }
