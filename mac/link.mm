#import "link.h"

#import "common.h"

namespace {

NSString* decoded(NSString* text) {
  NSString* spaced = [text stringByReplacingOccurrencesOfString:@"+" withString:@" "];
  return spaced.length ? [spaced stringByRemovingPercentEncoding] : @"";
}

bool textParam(NSString* kind, NSString* name) {
  if ([kind isEqualToString:@"command"]) return [name isEqualToString:@"action"];
  return [name isEqualToString:@"query"] || [name isEqualToString:@"source"] || [name isEqualToString:@"id"];
}

NSString* scheme() { return ns(std::wstring(APP_ID)); }

}

bool isOpenLink(NSString* link) {
  NSString* pattern = [NSString stringWithFormat:@"^%@://open/?$", scheme()];
  return [link rangeOfString:pattern options:NSRegularExpressionSearch].location != NSNotFound;
}

NSDictionary* requestOfLink(NSString* link) {
  NSString* pattern = [NSString stringWithFormat:@"^%@://(queue|command|dequeue)/?\\?(.*)$", scheme()];
  NSRegularExpression* regex = [NSRegularExpression regularExpressionWithPattern:pattern options:NSRegularExpressionDotMatchesLineSeparators error:nil];
  NSTextCheckingResult* match = [regex firstMatchInString:link options:0 range:NSMakeRange(0, link.length)];
  if (!match) return nil;
  NSString* kind = [link substringWithRange:[match rangeAtIndex:1]];
  NSString* query = [link substringWithRange:[match rangeAtIndex:2]];
  NSMutableDictionary* request = [NSMutableDictionary dictionary];
  NSMutableArray<NSString*>* pairs = [[query componentsSeparatedByString:@"&"] mutableCopy];
  if (!pairs.lastObject.length) [pairs removeLastObject];
  {
    for (NSString* pair in pairs) {
      const NSRange equals = [pair rangeOfString:@"="];
      if (equals.location == NSNotFound) return nil;
      NSString* name = decoded([pair substringToIndex:equals.location]);
      NSString* value = decoded([pair substringFromIndex:equals.location + 1]);
      if (!name || !value || request[name]) return nil;
      if (textParam(kind, name)) request[name] = value;
      else if ([kind isEqualToString:@"queue"] && [name isEqualToString:@"insert"] &&
               ([value isEqualToString:@"true"] || [value isEqualToString:@"false"]))
        request[name] = @([value isEqualToString:@"true"]);
      else return nil;
    }
  }
  return @{@"kind" : kind, @"request" : request};
}
