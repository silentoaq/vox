#import <Cocoa/Cocoa.h>

#include <memory>

#include "text.h"

#import "common.h"
#import "link.h"
#import "mainWindow.h"
#import "updater.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
  std::unique_ptr<MainWindow> _window;
  NSMutableArray<NSString*>* _pending;
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
  _pending = [NSMutableArray array];
  [NSAppleEventManager.sharedAppleEventManager setEventHandler:self
                                                   andSelector:@selector(handleUrl:withReply:)
                                                 forEventClass:kInternetEventClass
                                                    andEventID:kAEGetURL];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  if (launchStagedUpdate()) {
    [NSApp terminate:nil];
    return;
  }
  _window = std::make_unique<MainWindow>();
  _window->show();
  for (NSString* link in _pending) _window->openLink(link);
  [_pending removeAllObjects];
  [NSApp activateIgnoringOtherApps:YES];
}

- (void)handleUrl:(NSAppleEventDescriptor*)event withReply:(NSAppleEventDescriptor*)reply {
  NSString* link = [event paramDescriptorForKeyword:keyDirectObject].stringValue;
  if (!link) return;
  if (isOpenLink(link)) {
    if (_window) _window->activate();
    return;
  }
  if (_window) _window->openLink(link);
  else [_pending addObject:link];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  return YES;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)visible {
  if (_window) _window->activate();
  return NO;
}
@end

namespace {

NSMenuItem* item(NSString* title, SEL action, NSString* key) {
  return [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
}

NSMenu* buildMenu() {
  NSString* name = ns(std::wstring(APP_NAME));
  NSMenu* bar = [[NSMenu alloc] init];

  NSMenu* app = [[NSMenu alloc] initWithTitle:name];
  [app addItem:item([@"About " stringByAppendingString:name], @selector(orderFrontStandardAboutPanel:), @"")];
  [app addItem:NSMenuItem.separatorItem];
  [app addItem:item([@"Hide " stringByAppendingString:name], @selector(hide:), @"h")];
  NSMenuItem* others = item(@"Hide Others", @selector(hideOtherApplications:), @"h");
  others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  [app addItem:others];
  [app addItem:item(@"Show All", @selector(unhideAllApplications:), @"")];
  [app addItem:NSMenuItem.separatorItem];
  [app addItem:item([@"Quit " stringByAppendingString:name], @selector(terminate:), @"q")];

  NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
  [edit addItem:item(@"Undo", @selector(undo:), @"z")];
  NSMenuItem* redo = item(@"Redo", @selector(redo:), @"z");
  redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
  [edit addItem:redo];
  [edit addItem:NSMenuItem.separatorItem];
  [edit addItem:item(@"Cut", @selector(cut:), @"x")];
  [edit addItem:item(@"Copy", @selector(copy:), @"c")];
  [edit addItem:item(@"Paste", @selector(paste:), @"v")];
  [edit addItem:item(@"Select All", @selector(selectAll:), @"a")];

  NSMenu* window = [[NSMenu alloc] initWithTitle:@"Window"];
  [window addItem:item(@"Minimize", @selector(performMiniaturize:), @"m")];
  [window addItem:item(@"Zoom", @selector(performZoom:), @"")];
  [window addItem:item(@"Close", @selector(performClose:), @"w")];

  for (NSMenu* menu in @[ app, edit, window ]) {
    NSMenuItem* holder = [[NSMenuItem alloc] init];
    holder.submenu = menu;
    [bar addItem:holder];
  }
  NSApp.windowsMenu = window;
  return bar;
}

}

int main(int, const char**) {
  @autoreleasepool {
    NSApplication* app = NSApplication.sharedApplication;
    AppDelegate* delegate = [[AppDelegate alloc] init];
    app.delegate = delegate;
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    app.mainMenu = buildMenu();
    [app run];
  }
  return 0;
}
