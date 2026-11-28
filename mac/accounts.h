#pragma once
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <functional>

@class LoginWindowDelegate;

class Accounts {
public:
  using Changed = std::function<void(NSString* site)>;
  struct Site;

  Accounts(NSWindow* owner, WKWebsiteDataStore* store, Changed changed);
  void signedIn(NSString* site, std::function<void(bool)> done);
  void login(NSString* site);
  void logout(NSString* site, std::function<void()> done);
  void decorate(NSMutableURLRequest* request, std::function<void(NSMutableURLRequest*)> next);
  void netscapeCookies(NSString* site, std::function<void(NSString*)> done);
  void loginClosed();

private:
  static const Site& siteOf(NSString* id);
  void cookies(std::function<void(NSArray<NSHTTPCookie*>*)> done);
  void sessionOf(const Site& site, std::function<void(NSString*)> done);
  void poll();

  NSWindow* owner_;
  WKWebsiteDataStore* store_;
  Changed changed_;
  NSWindow* loginWindow_ = nil;
  WKWebView* loginView_ = nil;
  LoginWindowDelegate* loginDelegate_ = nil;
  NSTimer* pollTimer_ = nil;
  const Site* loginSite_ = nullptr;
  NSString* staleSession_ = nil;
};
