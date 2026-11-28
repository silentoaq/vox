#import "mainWindow.h"

#include "text.h"

#import "common.h"
#import "files.h"
#import "fonts.h"
#import "link.h"

namespace {

constexpr CGFloat kMinWidth = 960;
constexpr CGFloat kMinHeight = 620;
constexpr CGFloat kStartWidth = 1360;
constexpr CGFloat kStartHeight = 860;
NSString* const kSafariName = @"Version/17.6 Safari/605.1.15";
NSString* const kMessageName = @"vox";

NSString* pageOrigin() { return [NSString stringWithFormat:@"http://localhost:%d", kServerPort]; }

NSString* const kHostShim = @(R"JS((() => {
  const listeners = [];
  window.__voxReceive = (data) => {
    for (const listener of listeners) listener({ data });
  };
  window.chrome = {
    webview: {
      postMessage: (message) => window.webkit.messageHandlers.vox.postMessage(message),
      addEventListener: (type, listener) => {
        if (type !== 'message') throw new Error(`不支援的事件 ${type}`);
        listeners.push(listener);
      },
    },
  };
})();)JS");

NSString* hexOfColor(uint32_t rgb) { return [NSString stringWithFormat:@"#%06X", rgb & 0xFFFFFF]; }

uint32_t colorOfHex(NSString* hex) {
  NSCharacterSet* digits = [NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"];
  if (hex.length != 7 || ![hex hasPrefix:@"#"] ||
      [[hex substringFromIndex:1] rangeOfCharacterFromSet:digits.invertedSet].location != NSNotFound)
    throw HostError([@"顏色格式不對：" stringByAppendingString:hex]);
  return uint32_t(strtoul([hex substringFromIndex:1].UTF8String, nullptr, 16));
}

std::vector<LyricLine> lyricLinesOf(NSArray* items) {
  std::vector<LyricLine> lines;
  for (NSDictionary* item in items) {
    LyricLine line{[field<NSNumber>(item, @"start") doubleValue], field<NSString>(item, @"text"), {}};
    id chars = item[@"chars"];
    if ([chars isKindOfClass:NSArray.class]) {
      for (NSDictionary* c in chars)
        line.chars.push_back({[field<NSNumber>(c, @"t") doubleValue], [field<NSNumber>(c, @"e") doubleValue],
                              [field<NSNumber>(c, @"pos") unsignedIntegerValue], [field<NSNumber>(c, @"len") unsignedIntegerValue]});
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

NSString* readScript(NSString* relative) {
  NSString* text = readTextFile([[resourcesDir() stringByAppendingPathComponent:@"ui"] stringByAppendingPathComponent:relative]);
  if (!text) throw HostError(ns(tr(Text::StepReadPlayerScript)));
  return text;
}

NSString* frameScript() {
  NSString* worklet = readScript(@"vox-pitch-worklet.js");
  NSString* graph = [@"\n" stringByAppendingString:readScript(@"js/effectsGraph.js")];
  if ([graph rangeOfString:@"\nexport "].location == NSNotFound) throw HostError(ns(tr(Text::StepParseEffects)));
  graph = [graph stringByReplacingOccurrencesOfString:@"\nexport " withString:@"\n"];
  NSData* quoted = [NSJSONSerialization dataWithJSONObject:worklet options:NSJSONWritingFragmentsAllowed error:nil];
  return [NSString stringWithFormat:@"(() => {\nconst pitchWorkletUrl = URL.createObjectURL(new Blob([%@], { type: 'text/javascript' }));\n%@\n%@\n})();",
                                    [[NSString alloc] initWithData:quoted encoding:NSUTF8StringEncoding], graph,
                                    readScript(@"inject/embedFrame.js")];
}

bool editItem(NSMenuItem* item) {
  for (NSString* kept in @[ @"Copy", @"Cut", @"Paste", @"SelectAll" ])
    if ([item.identifier hasSuffix:kept]) return true;
  return false;
}

}

@interface VoxWebView : WKWebView
@end

@implementation VoxWebView
- (void)willOpenMenu:(NSMenu*)menu withEvent:(NSEvent*)event {
  [super willOpenMenu:menu withEvent:event];
  if (VOX_DEVTOOLS) return;
  for (NSMenuItem* item in [menu.itemArray copy])
    if (!editItem(item)) [menu removeItem:item];
}
@end

@interface WebViewHost : NSObject <WKScriptMessageHandler, WKNavigationDelegate, WKUIDelegate, NSWindowDelegate>
@property(nonatomic) MainWindow* owner;
@end

@implementation WebViewHost
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message {
  self.owner->receive(message);
}

- (void)webView:(WKWebView*)webView
  decidePolicyForNavigationAction:(WKNavigationAction*)action
                  decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
  NSURL* url = action.request.URL;
  const bool ours = [[NSString stringWithFormat:@"%@://%@:%@", url.scheme, url.host, url.port] isEqualToString:pageOrigin()];
  if (!action.targetFrame.isMainFrame || ours) {
    decisionHandler(WKNavigationActionPolicyAllow);
    return;
  }
  if ([url.scheme isEqualToString:@"https"] || [url.scheme isEqualToString:@"http"]) [NSWorkspace.sharedWorkspace openURL:url];
  decisionHandler(WKNavigationActionPolicyCancel);
}

- (WKWebView*)webView:(WKWebView*)webView
  createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
             forNavigationAction:(WKNavigationAction*)action
                  windowFeatures:(WKWindowFeatures*)features {
  NSURL* url = action.request.URL;
  if ([url.scheme isEqualToString:@"https"] || [url.scheme isEqualToString:@"http"]) [NSWorkspace.sharedWorkspace openURL:url];
  return nil;
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
  [webView reload];
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
  self.owner->shownChanged();
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
  self.owner->shownChanged();
}

- (void)windowDidChangeOcclusionState:(NSNotification*)notification {
  self.owner->shownChanged();
}
@end

MainWindow::MainWindow() {
  const NSRect work = NSScreen.mainScreen.visibleFrame;
  const CGFloat width = std::min(kStartWidth, work.size.width);
  const CGFloat height = std::min(kStartHeight, work.size.height);
  window_ = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                                                  NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  window_.releasedWhenClosed = NO;
  window_.title = ns(std::wstring(APP_NAME));
  window_.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  window_.backgroundColor = [NSColor colorWithSRGBRed:0x0d / 255.0 green:0x0d / 255.0 blue:0x10 / 255.0 alpha:1];
  window_.contentMinSize = NSMakeSize(kMinWidth, kMinHeight);
  window_.tabbingMode = NSWindowTabbingModeDisallowed;
  [window_ center];
  window_.frameAutosaveName = @"main";
  host_ = [[WebViewHost alloc] init];
  host_.owner = this;
  window_.delegate = host_;

  WKWebsiteDataStore* store = WKWebsiteDataStore.defaultDataStore;
  accounts_ = std::make_unique<Accounts>(window_, store, [this](NSString* site) { bridge_->emit(@"account", @{@"site" : site}); });
  bridge_ = std::make_unique<Bridge>(
    [this](NSString* json) {
      [webView_ evaluateJavaScript:[NSString stringWithFormat:@"window.__voxReceive(%@)", json] completionHandler:nil];
    },
    [this](NSMutableURLRequest* request, std::function<void(NSMutableURLRequest*)> next) {
      accounts_->decorate(request, std::move(next));
    });
  lyrics_ = std::make_unique<DesktopLyrics>([this](NSString* action) {
    if ([action isEqualToString:@"state"]) {
      bridge_->emit(@"desktopLyricsState", desktopLyricsState());
      return;
    }
    control(action);
  });
  karaoke_ = std::make_unique<Karaoke>([this](const KaraokeStatus& status) {
    bridge_->emit(@"karaoke", @{
      @"id" : status.id,
      @"state" : status.state,
      @"segment" : @(status.segment),
      @"total" : @(status.total),
      @"stride" : @(status.stride),
      @"samples" : @(status.samples),
      @"message" : status.message ?: @"",
      @"progress" : @(status.progress),
    });
  });
  nowPlaying_ = std::make_unique<NowPlaying>([this](NSString* action) { control(action); });
  updater_ = std::make_unique<Updater>([this](const UpdateStatus& status) {
    bridge_->emit(@"update", @{@"state" : status.state, @"version" : status.version ?: @"", @"message" : status.message ?: @""});
  });
  registerMethods();
  createWebView();
}

void MainWindow::control(NSString* action) { bridge_->emit(@"control", @{@"action" : action}); }

NSDictionary* MainWindow::desktopLyricsState() const {
  const LyricLook& look = lyrics_->look();
  return @{
    @"visible" : @(lyrics_->visible()),
    @"locked" : @(lyrics_->locked()),
    @"sung" : hexOfColor(look.sung),
    @"unsung" : hexOfColor(look.unsung),
    @"outline" : hexOfColor(look.outline),
    @"outlineWidth" : @(look.outlineWidth),
    @"weight" : @(look.weight),
    @"backgroundOn" : @(look.backgroundOn),
    @"background" : hexOfColor(look.background),
  };
}

NSDictionary* MainWindow::languageState() const { return @{@"setting" : ns(languageSetting()), @"active" : ns(languageId())}; }

NSDictionary* MainWindow::windowState() const { return @{@"shown" : @(windowShown_)}; }

void MainWindow::shownChanged() {
  const bool shown = window_.visible && !window_.miniaturized && (window_.occlusionState & NSWindowOcclusionStateVisible);
  if (shown == windowShown_) return;
  windowShown_ = shown;
  if (pageReady_) bridge_->emit(@"window", windowState());
}

void MainWindow::registerMethods() {
  bridge_->on(@"ready", [this](NSDictionary*) -> id {
    pageReady_ = true;
    sendPendingLinks();
    updater_->started();
    return @{
      @"desktopLyrics" : desktopLyricsState(),
      @"window" : windowState(),
      @"stems" : [pageOrigin() stringByAppendingString:@"/stems"],
    };
  });
  bridge_->on(@"language.get", [this](NSDictionary*) -> id { return languageState(); });
  bridge_->on(@"language.set", [this](NSDictionary* params) -> id {
    NSString* setting = field<NSString>(params, @"setting");
    if (!chooseLanguage(toWide(setting))) throw HostError([@"沒有這個語言：" stringByAppendingString:setting]);
    lyrics_->languageChanged();
    return languageState();
  });
  bridge_->on(@"nowPlaying.track", [this](NSDictionary* params) -> id {
    NSString* title = field<NSString>(params, @"title");
    if (!title.length) nowPlaying_->clearTrack();
    else
      nowPlaying_->setTrack(title, field<NSString>(params, @"artist"), field<NSString>(params, @"album"), field<NSString>(params, @"cover"));
    return @{};
  });
  bridge_->on(@"nowPlaying.playback", [this](NSDictionary* params) -> id {
    nowPlaying_->setPlayback(field<NSString>(params, @"state"), [field<NSNumber>(params, @"position") doubleValue],
                             [field<NSNumber>(params, @"duration") doubleValue]);
    return @{};
  });
  bridge_->on(@"nowPlaying.lyrics", [this](NSDictionary* params) -> id {
    nowPlaying_->setLyrics(field<NSString>(params, @"text"));
    return @{};
  });
  bridge_->onAsync(@"karaoke.prepare", [this](NSDictionary* params, Bridge::Respond respond, Bridge::Fail) {
    NSString* song = field<NSString>(params, @"id");
    const double from = [field<NSNumber>(params, @"from") doubleValue];
    const bool urgent = [field<NSNumber>(params, @"urgent") boolValue];
    accounts_->signedIn(@"youtube", [this, song, from, urgent, respond](bool signedIn) {
      if (!signedIn) {
        karaoke_->prepare(song, from, urgent, nil);
        respond(@{});
        return;
      }
      accounts_->netscapeCookies(@"youtube", [this, song, from, urgent, respond](NSString* cookies) {
        karaoke_->prepare(song, from, urgent, cookies);
        respond(@{});
      });
    });
  });
  bridge_->onAsync(@"account.state", [this](NSDictionary* params, Bridge::Respond respond, Bridge::Fail) {
    accounts_->signedIn(field<NSString>(params, @"site"), [respond](bool signedIn) { respond(@{@"signedIn" : @(signedIn)}); });
  });
  bridge_->on(@"account.login", [this](NSDictionary* params) -> id {
    accounts_->login(field<NSString>(params, @"site"));
    return @{};
  });
  bridge_->onAsync(@"account.logout", [this](NSDictionary* params, Bridge::Respond respond, Bridge::Fail) {
    accounts_->logout(field<NSString>(params, @"site"), [respond] { respond(@{}); });
  });
  bridge_->on(@"library.load", [](NSDictionary*) -> id { return @{@"text" : readTextFile(appDataPath(@"library.json")) ?: @""}; });
  bridge_->on(@"library.save", [](NSDictionary* params) -> id {
    writeTextFile(appDataPath(@"library.json"), field<NSString>(params, @"text"));
    return @{};
  });
  bridge_->on(@"desktopLyrics.song", [this](NSDictionary* params) -> id {
    lyrics_->setSong(field<NSString>(params, @"title"), field<NSString>(params, @"artist"), lyricLinesOf(field<NSArray>(params, @"lines")));
    return @{};
  });
  bridge_->on(@"desktopLyrics.clock", [this](NSDictionary* params) -> id {
    lyrics_->setClock([field<NSNumber>(params, @"ms") doubleValue], [field<NSNumber>(params, @"playing") boolValue]);
    return @{};
  });
  bridge_->onAsync(@"desktopLyrics.font", [this](NSDictionary* params, Bridge::Respond respond, Bridge::Fail fail) {
    const unsigned serial = ++fontSerial_;
    if (!params[@"id"]) {
      lyrics_->setFont(nil);
      respond(@{});
      return;
    }
    FontRequest request{field<NSString>(params, @"id"), field<NSString>(params, @"weight"), params[@"url"] ?: @"", params[@"gfont"] ?: @"",
                        params[@"zipPath"] ?: @""};
    inBackground(^{
      FontFile file = fetchFontFile(request);
      onMain(^{
        if (file.error) {
          fail(file.error);
          return;
        }
        if (serial == fontSerial_) {
          try {
            lyrics_->setFont(file.path);
          } catch (const std::exception& e) {
            [NSFileManager.defaultManager removeItemAtPath:file.path error:nil];
            fail(ns(std::string(e.what())));
            return;
          }
        }
        respond(@{});
      });
    });
  });
  bridge_->on(@"desktopLyrics.look", [this](NSDictionary* params) -> id {
    LyricLook look;
    look.sung = colorOfHex(field<NSString>(params, @"sung"));
    look.unsung = colorOfHex(field<NSString>(params, @"unsung"));
    look.outline = colorOfHex(field<NSString>(params, @"outline"));
    look.outlineWidth = [field<NSNumber>(params, @"outlineWidth") floatValue];
    look.weight = [field<NSNumber>(params, @"weight") intValue];
    look.backgroundOn = [field<NSNumber>(params, @"backgroundOn") boolValue];
    look.background = colorOfHex(field<NSString>(params, @"background"));
    lyrics_->setLook(look, [field<NSNumber>(params, @"save") boolValue]);
    return @{};
  });
  bridge_->on(@"desktopLyrics.set", [this](NSDictionary* params) -> id {
    if (params[@"visible"]) lyrics_->setVisible([field<NSNumber>(params, @"visible") boolValue]);
    if (params[@"locked"]) lyrics_->setLocked([field<NSNumber>(params, @"locked") boolValue]);
    return desktopLyricsState();
  });
}

void MainWindow::receive(WKScriptMessage* message) {
  WKSecurityOrigin* origin = message.frameInfo.securityOrigin;
  if (!message.frameInfo.isMainFrame || ![origin.protocol isEqualToString:@"http"] || ![origin.host isEqualToString:@"localhost"] ||
      origin.port != kServerPort)
    return;
  if (![message.body isKindOfClass:NSDictionary.class]) {
    NSLog(@"頁面訊息格式錯誤：不是物件");
    return;
  }
  try {
    bridge_->handle(message.body);
  } catch (const std::exception& e) {
    NSLog(@"頁面訊息格式錯誤：%s", e.what());
  }
}

void MainWindow::show() { [window_ makeKeyAndOrderFront:nil]; }

void MainWindow::activate() {
  if (window_.miniaturized) [window_ deminiaturize:nil];
  [window_ makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

void MainWindow::openLink(NSString* link) {
  NSDictionary* request = requestOfLink(link);
  if (!request) {
    NSLog(@"看不懂的連結：%@", link);
    return;
  }
  [pendingLinks_ addObject:request];
  if (pageReady_) sendPendingLinks();
}

void MainWindow::sendPendingLinks() {
  for (NSDictionary* link in pendingLinks_) bridge_->emit(@"link", link);
  [pendingLinks_ removeAllObjects];
}

void MainWindow::fatal(NSString* message) {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleCritical;
  alert.messageText = message;
  [alert runModal];
  [NSApp terminate:nil];
}

void MainWindow::createWebView() {
  NSString* script = nil;
  try {
    script = frameScript();
  } catch (const std::exception& e) {
    fatal(ns(tr(Text::StartFailedReason, {APP_NAME, toWide(ns(std::string(e.what())))})));
    return;
  }
  server_ = std::make_unique<Server>([resourcesDir() stringByAppendingPathComponent:@"ui"], Karaoke::stemsDir());
  if (NSString* failure = server_->start()) {
    fatal(ns(tr(Text::StartFailedReason, {APP_NAME, toWide(failure)})));
    return;
  }

  WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
  config.websiteDataStore = WKWebsiteDataStore.defaultDataStore;
  config.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
  config.applicationNameForUserAgent = kSafariName;
  WKUserContentController* content = config.userContentController;
  [content addScriptMessageHandler:host_ name:kMessageName];
  [content addUserScript:[[WKUserScript alloc] initWithSource:kHostShim injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
  [content addUserScript:[[WKUserScript alloc] initWithSource:script injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:NO]];

  webView_ = [[VoxWebView alloc] initWithFrame:window_.contentView.bounds configuration:config];
  webView_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  webView_.navigationDelegate = host_;
  webView_.UIDelegate = host_;
  if (@available(macOS 13.3, *)) webView_.inspectable = VOX_DEVTOOLS;
  [window_.contentView addSubview:webView_];
  [webView_ loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:[pageOrigin() stringByAppendingString:@"/index.html"]]]];
}
