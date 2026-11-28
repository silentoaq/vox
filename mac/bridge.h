#pragma once
#import <Foundation/Foundation.h>

#import "common.h"

#include <functional>
#include <map>
#include <string>

class Bridge {
public:
  using Respond = std::function<void(id result)>;
  using Fail = std::function<void(NSString* error)>;
  using Handler = std::function<id(NSDictionary* params)>;
  using AsyncHandler = std::function<void(NSDictionary* params, Respond respond, Fail fail)>;
  using Send = std::function<void(NSString* json)>;
  using Decorate = std::function<void(NSMutableURLRequest* request, std::function<void(NSMutableURLRequest*)> next)>;

  Bridge(Send send, Decorate decorate);
  void on(NSString* method, Handler handler);
  void onAsync(NSString* method, AsyncHandler handler);
  void handle(NSDictionary* message);
  void emit(NSString* event, id params);

private:
  void startHttp(NSNumber* call, NSDictionary* params);
  void reply(NSNumber* call, id result);
  void fail(NSNumber* call, NSString* error);
  void send(NSDictionary* message);

  Send send_;
  Decorate decorate_;
  std::map<std::string, Handler> handlers_;
  std::map<std::string, AsyncHandler> asyncHandlers_;
};

NSString* hostOf(NSString* url);

template <typename T>
T* field(NSDictionary* params, NSString* name) {
  id value = params[name];
  if (![value isKindOfClass:[T class]]) throw HostError([NSString stringWithFormat:@"欄位 %@ 的型別不對", name]);
  return value;
}
