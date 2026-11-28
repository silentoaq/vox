#pragma once
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <memory>

#import "accounts.h"
#import "bridge.h"
#import "desktopLyrics.h"
#import "karaoke.h"
#import "nowPlaying.h"
#import "server.h"
#import "updater.h"

@class WebViewHost;

class MainWindow {
public:
  MainWindow();
  void show();
  void openLink(NSString* link);
  void activate();
  void receive(WKScriptMessage* message);
  void shownChanged();

private:
  void registerMethods();
  void createWebView();
  void control(NSString* action);
  void sendPendingLinks();
  void fatal(NSString* message);
  NSDictionary* desktopLyricsState() const;
  NSDictionary* languageState() const;
  NSDictionary* windowState() const;

  NSWindow* window_ = nil;
  WKWebView* webView_ = nil;
  WebViewHost* host_ = nil;
  std::unique_ptr<Server> server_;
  std::unique_ptr<Accounts> accounts_;
  std::unique_ptr<Bridge> bridge_;
  std::unique_ptr<DesktopLyrics> lyrics_;
  std::unique_ptr<Karaoke> karaoke_;
  std::unique_ptr<NowPlaying> nowPlaying_;
  std::unique_ptr<Updater> updater_;
  bool pageReady_ = false;
  bool windowShown_ = true;
  unsigned fontSerial_ = 0;
  NSMutableArray<NSDictionary*>* pendingLinks_ = [NSMutableArray array];
};
