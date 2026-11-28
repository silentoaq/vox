#import "bridge.h"

#import "common.h"
#import "http.h"

namespace {

NSSet<NSString*>* reachableHosts() {
  static NSSet* hosts = [NSSet setWithArray:@[
    @"music.youtube.com", @"www.youtube.com", @"mobilecdn.kugou.com", @"krcs.kugou.com", @"lyrics.kugou.com", @"open.spotify.com",
    @"open.spotifycdn.com", @"api-partner.spotify.com"
  ]];
  return hosts;
}

}

NSString* hostOf(NSString* url) { return [NSURL URLWithString:url].host ?: @""; }

Bridge::Bridge(Send send, Decorate decorate) : send_(std::move(send)), decorate_(std::move(decorate)) {}

void Bridge::on(NSString* method, Handler handler) { handlers_[method.UTF8String] = std::move(handler); }

void Bridge::onAsync(NSString* method, AsyncHandler handler) { asyncHandlers_[method.UTF8String] = std::move(handler); }

void Bridge::handle(NSDictionary* message) {
  NSNumber* call = field<NSNumber>(message, @"id");
  NSString* method = field<NSString>(message, @"method");
  NSDictionary* params = field<NSDictionary>(message, @"params");
  try {
    if ([method isEqualToString:@"http"]) {
      startHttp(call, params);
      return;
    }
    if (auto async = asyncHandlers_.find(method.UTF8String); async != asyncHandlers_.end()) {
      async->second(params, [this, call](id result) { reply(call, result); }, [this, call](NSString* error) { fail(call, error); });
      return;
    }
    auto handler = handlers_.find(method.UTF8String);
    if (handler == handlers_.end()) {
      fail(call, [@"沒有這個方法：" stringByAppendingString:method]);
      return;
    }
    reply(call, handler->second(params));
  } catch (const std::exception& e) {
    fail(call, ns(std::string(e.what())));
  }
}

void Bridge::startHttp(NSNumber* call, NSDictionary* params) {
  NSString* url = field<NSString>(params, @"url");
  NSString* host = hostOf(url);
  if (![reachableHosts() containsObject:host]) {
    fail(call, [@"不允許連到 " stringByAppendingString:host]);
    return;
  }
  NSMutableURLRequest* request = httpRequest(url);
  request.HTTPMethod = field<NSString>(params, @"method");
  NSString* body = field<NSString>(params, @"body");
  if (body.length) request.HTTPBody = [body dataUsingEncoding:NSUTF8StringEncoding];
  NSDictionary* headers = field<NSDictionary>(params, @"headers");
  for (NSString* name in headers) [request setValue:headers[name] forHTTPHeaderField:name];

  auto send = [this, call](NSMutableURLRequest* ready) {
    httpFetch(ready, ^(HttpResult result) {
      onMain(^{
        if (result.error) {
          fail(call, result.error);
          return;
        }
        NSString* text = [[NSString alloc] initWithData:result.body encoding:NSUTF8StringEncoding];
        if (!text) {
          fail(call, [hostOf(ready.URL.absoluteString) stringByAppendingString:@" 回的內容不是 UTF-8"]);
          return;
        }
        reply(call, @{@"status" : @(result.status), @"body" : text});
      });
    });
  };
  if ([field<NSNumber>(params, @"anonymous") boolValue]) send(request);
  else decorate_(request, send);
}

void Bridge::send(NSDictionary* message) {
  NSError* error = nil;
  NSData* data = [NSJSONSerialization dataWithJSONObject:message options:0 error:&error];
  if (!data) throw HostError(error.localizedDescription);
  send_([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]);
}

void Bridge::emit(NSString* event, id params) { send(@{@"event" : event, @"params" : params}); }

void Bridge::reply(NSNumber* call, id result) { send(@{@"id" : call, @"result" : result}); }

void Bridge::fail(NSNumber* call, NSString* error) { send(@{@"id" : call, @"error" : error}); }
