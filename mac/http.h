#pragma once
#import <Foundation/Foundation.h>

struct HttpResult {
  NSInteger status = 0;
  NSData* body = nil;
  NSString* error = nil;
};

NSMutableURLRequest* httpRequest(NSString* url);
void httpFetch(NSURLRequest* request, void (^done)(HttpResult result));
HttpResult httpFetchNow(NSURLRequest* request);
NSString* httpDownloadNow(NSString* url, NSString* path);
