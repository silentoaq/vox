#import "accounts.h"

#import <CommonCrypto/CommonDigest.h>

#include <vector>

#include "text.h"

#import "bridge.h"
#import "common.h"

struct Accounts::Site {
  NSString* id;
  NSString* name;
  NSString* loginUrl;
  NSString* home;
  NSArray<NSString*>* sessionCookies;
  NSArray<NSString*>* domains;
  NSArray<NSString*>* brands;
  bool signsRequests;
};

@interface LoginWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) Accounts* accounts;
@end

@implementation LoginWindowDelegate
- (void)windowWillClose:(NSNotification*)notification {
  self.accounts->loginClosed();
}
@end

namespace {

constexpr NSTimeInterval kPollSeconds = 1;
constexpr CGFloat kLoginWidth = 520;
constexpr CGFloat kLoginHeight = 760;
NSString* const kSafariName = @"Version/17.6 Safari/605.1.15";

const std::vector<Accounts::Site>& sites() {
  static const std::vector<Accounts::Site> all = {
    {@"youtube", @"YouTube", @"https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F",
     @"https://music.youtube.com", @[ @"SAPISID", @"__Secure-3PAPISID" ], @[ @"youtube.com" ], @[ @"google" ], true},
    {@"spotify", @"Spotify", @"https://accounts.spotify.com/login?continue=https%3A%2F%2Fopen.spotify.com%2F", @"https://open.spotify.com",
     @[ @"sp_dc" ], @[ @"spotify.com" ], @[], false},
  };
  return all;
}

bool belongs(const Accounts::Site& site, NSString* domain) {
  if ([domain hasPrefix:@"."]) domain = [domain substringFromIndex:1];
  for (NSString* suffix in site.domains)
    if ([domain isEqualToString:suffix] || [domain hasSuffix:[@"." stringByAppendingString:suffix]]) return true;
  for (NSString* brand in site.brands)
    if ([domain hasPrefix:[brand stringByAppendingString:@"."]] ||
        [domain rangeOfString:[NSString stringWithFormat:@".%@.", brand]].location != NSNotFound)
      return true;
  return false;
}

bool sentTo(NSHTTPCookie* cookie, NSURL* url) {
  NSString* domain = cookie.domain;
  NSString* host = url.host;
  const bool hostMatch = [domain hasPrefix:@"."] ? ([host isEqualToString:[domain substringFromIndex:1]] || [host hasSuffix:domain])
                                                 : [host isEqualToString:domain];
  if (!hostMatch) return false;
  if (cookie.isSecure && ![url.scheme isEqualToString:@"https"]) return false;
  if (cookie.expiresDate && cookie.expiresDate.timeIntervalSinceNow < 0) return false;
  NSString* path = url.path.length ? url.path : @"/";
  return [path hasPrefix:cookie.path];
}

NSString* sessionValue(const Accounts::Site& site, NSArray<NSHTTPCookie*>* jar) {
  NSURL* home = [NSURL URLWithString:site.home];
  for (NSString* name in site.sessionCookies)
    for (NSHTTPCookie* cookie in jar)
      if ([cookie.name isEqualToString:name] && cookie.value.length && sentTo(cookie, home)) return cookie.value;
  return @"";
}

NSString* sha1Hex(NSString* text) {
  NSData* data = [text dataUsingEncoding:NSUTF8StringEncoding];
  unsigned char digest[CC_SHA1_DIGEST_LENGTH];
  CC_SHA1(data.bytes, CC_LONG(data.length), digest);
  NSMutableString* out = [NSMutableString string];
  for (unsigned char byte : digest) [out appendFormat:@"%02x", byte];
  return out;
}

}

Accounts::Accounts(NSWindow* owner, WKWebsiteDataStore* store, Changed changed)
    : owner_(owner), store_(store), changed_(std::move(changed)) {}

const Accounts::Site& Accounts::siteOf(NSString* id) {
  for (const Site& site : sites())
    if ([id isEqualToString:site.id]) return site;
  throw HostError([@"沒有這種帳號：" stringByAppendingString:id]);
}

void Accounts::cookies(std::function<void(NSArray<NSHTTPCookie*>*)> done) {
  [store_.httpCookieStore getAllCookies:^(NSArray<NSHTTPCookie*>* all) {
    done(all);
  }];
}

void Accounts::sessionOf(const Site& site, std::function<void(NSString*)> done) {
  cookies([&site, done](NSArray<NSHTTPCookie*>* jar) { done(sessionValue(site, jar)); });
}

void Accounts::signedIn(NSString* id, std::function<void(bool)> done) {
  sessionOf(siteOf(id), [done](NSString* value) { done(value.length > 0); });
}

void Accounts::login(NSString* id) {
  const Site& site = siteOf(id);
  if (loginSite_) {
    [loginWindow_ makeKeyAndOrderFront:nil];
    return;
  }
  loginSite_ = &site;
  sessionOf(site, [this](NSString* stale) {
    staleSession_ = stale;
    const NSRect frame = owner_.frame;
    const NSRect rect = NSMakeRect(NSMidX(frame) - kLoginWidth / 2, NSMidY(frame) - kLoginHeight / 2, kLoginWidth, kLoginHeight);
    loginWindow_ = [[NSWindow alloc] initWithContentRect:rect
                                               styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    loginWindow_.releasedWhenClosed = NO;
    loginWindow_.title = ns(tr(Text::LoginTitle, {wide(loginSite_->name)}));
    loginWindow_.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    loginDelegate_ = [[LoginWindowDelegate alloc] init];
    loginDelegate_.accounts = this;
    loginWindow_.delegate = loginDelegate_;
    WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
    config.websiteDataStore = store_;
    config.applicationNameForUserAgent = kSafariName;
    loginView_ = [[WKWebView alloc] initWithFrame:loginWindow_.contentView.bounds configuration:config];
    loginView_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [loginWindow_.contentView addSubview:loginView_];
    [loginView_ loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:loginSite_->loginUrl]]];
    [loginWindow_ makeKeyAndOrderFront:nil];
    pollTimer_ = [NSTimer scheduledTimerWithTimeInterval:kPollSeconds
                                                 repeats:YES
                                                   block:^(NSTimer*) {
                                                     poll();
                                                   }];
  });
}

void Accounts::poll() {
  if (!loginView_ || ![loginView_.URL.absoluteString hasPrefix:loginSite_->home]) return;
  NSWindow* window = loginWindow_;
  sessionOf(*loginSite_, [this, window](NSString* value) {
    if (window == loginWindow_ && value.length && ![value isEqualToString:staleSession_]) [window close];
  });
}

void Accounts::loginClosed() {
  [pollTimer_ invalidate];
  pollTimer_ = nil;
  [loginView_ stopLoading];
  loginView_ = nil;
  NSString* id = loginSite_->id;
  loginWindow_.delegate = nil;
  loginWindow_ = nil;
  loginDelegate_ = nil;
  loginSite_ = nullptr;
  changed_(id);
}

void Accounts::logout(NSString* id, std::function<void()> done) {
  const Site& site = siteOf(id);
  cookies([this, &site, done](NSArray<NSHTTPCookie*>* jar) {
    dispatch_group_t group = dispatch_group_create();
    for (NSHTTPCookie* cookie in jar) {
      if (!belongs(site, cookie.domain)) continue;
      dispatch_group_enter(group);
      [store_.httpCookieStore deleteCookie:cookie
                         completionHandler:^{
                           dispatch_group_leave(group);
                         }];
    }
    dispatch_group_notify(group, dispatch_get_main_queue(), ^{
      done();
    });
  });
}

void Accounts::netscapeCookies(NSString* id, std::function<void(NSString*)> done) {
  const Site& site = siteOf(id);
  cookies([&site, done](NSArray<NSHTTPCookie*>* jar) {
    NSMutableString* out = [NSMutableString stringWithString:@"# Netscape HTTP Cookie File\n"];
    for (NSHTTPCookie* cookie in jar) {
      if (!belongs(site, cookie.domain)) continue;
      const long long expires = cookie.isSessionOnly ? 0 : (long long)cookie.expiresDate.timeIntervalSince1970;
      [out appendFormat:@"%@%@\t%@\t%@\t%@\t%lld\t%@\t%@\n", cookie.isHTTPOnly ? @"#HttpOnly_" : @"", cookie.domain,
                        [cookie.domain hasPrefix:@"."] ? @"TRUE" : @"FALSE", cookie.path, cookie.isSecure ? @"TRUE" : @"FALSE", expires,
                        cookie.name, cookie.value];
    }
    done(out);
  });
}

void Accounts::decorate(NSMutableURLRequest* request, std::function<void(NSMutableURLRequest*)> next) {
  const Site* site = nullptr;
  for (const Site& candidate : sites())
    if ([hostOf(candidate.home) isEqualToString:request.URL.host]) site = &candidate;
  if (!site) {
    next(request);
    return;
  }
  NSURL* home = [NSURL URLWithString:site->home];
  cookies([site, request, next, home](NSArray<NSHTTPCookie*>* jar) {
    NSMutableArray* pairs = [NSMutableArray array];
    for (NSHTTPCookie* cookie in jar)
      if (sentTo(cookie, home)) [pairs addObject:[NSString stringWithFormat:@"%@=%@", cookie.name, cookie.value]];
    if (pairs.count) [request setValue:[pairs componentsJoinedByString:@"; "] forHTTPHeaderField:@"Cookie"];
    NSString* session = sessionValue(*site, jar);
    if (site->signsRequests && session.length) {
      NSString* stamp = [NSString stringWithFormat:@"%lld", (long long)NSDate.date.timeIntervalSince1970];
      NSString* hash = sha1Hex([NSString stringWithFormat:@"%@ %@ %@", stamp, session, site->home]);
      [request setValue:[NSString stringWithFormat:@"SAPISIDHASH %@_%@", stamp, hash] forHTTPHeaderField:@"Authorization"];
      [request setValue:site->home forHTTPHeaderField:@"X-Origin"];
      [request setValue:@"0" forHTTPHeaderField:@"X-Goog-AuthUser"];
    }
    next(request);
  });
}
