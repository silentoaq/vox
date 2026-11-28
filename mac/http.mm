#import "http.h"

namespace {

NSString* const kUserAgent =
  @"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36";

NSURLSession* session() {
  static NSURLSession* shared = [] {
    NSURLSessionConfiguration* config = NSURLSessionConfiguration.ephemeralSessionConfiguration;
    config.HTTPShouldSetCookies = NO;
    config.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
    config.HTTPCookieStorage = nil;
    config.URLCache = nil;
    return [NSURLSession sessionWithConfiguration:config];
  }();
  return shared;
}

}

NSMutableURLRequest* httpRequest(NSString* url) {
  NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url]];
  [request setValue:kUserAgent forHTTPHeaderField:@"User-Agent"];
  return request;
}

void httpFetch(NSURLRequest* request, void (^done)(HttpResult result)) {
  [[session() dataTaskWithRequest:request
                completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                  HttpResult result;
                  if (error) result.error = error.localizedDescription;
                  else {
                    result.status = static_cast<NSHTTPURLResponse*>(response).statusCode;
                    result.body = data ?: [NSData data];
                  }
                  done(result);
                }] resume];
}

HttpResult httpFetchNow(NSURLRequest* request) {
  dispatch_semaphore_t finished = dispatch_semaphore_create(0);
  __block HttpResult out;
  httpFetch(request, ^(HttpResult result) {
    out = result;
    dispatch_semaphore_signal(finished);
  });
  dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);
  return out;
}

NSString* httpDownloadNow(NSString* url, NSString* path) {
  dispatch_semaphore_t finished = dispatch_semaphore_create(0);
  __block NSString* failure = nil;
  [[session() downloadTaskWithRequest:httpRequest(url)
                    completionHandler:^(NSURL* location, NSURLResponse* response, NSError* error) {
                      if (error) failure = error.localizedDescription;
                      else if (static_cast<NSHTTPURLResponse*>(response).statusCode != 200)
                        failure = [NSString stringWithFormat:@"伺服器回 %ld", long(static_cast<NSHTTPURLResponse*>(response).statusCode)];
                      else {
                        NSError* moveError = nil;
                        [NSFileManager.defaultManager removeItemAtPath:path error:nil];
                        if (![NSFileManager.defaultManager moveItemAtURL:location toURL:[NSURL fileURLWithPath:path] error:&moveError])
                          failure = moveError.localizedDescription;
                      }
                      dispatch_semaphore_signal(finished);
                    }] resume];
  dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);
  return failure;
}
