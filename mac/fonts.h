#pragma once
#import <Foundation/Foundation.h>

struct FontRequest {
  NSString* id;
  NSString* weight;
  NSString* url;
  NSString* gfont;
  NSString* zipPath;
};

struct FontFile {
  NSString* path;
  NSString* error;
};

FontFile fetchFontFile(const FontRequest& request);
