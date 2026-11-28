#import "desktopLyrics.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "text.h"

#import "common.h"
#import "files.h"

namespace {

constexpr NSTimeInterval kLockPollSeconds = 0.15;
constexpr double kFrameMs = 33;
constexpr double kFadeMs = 250;
constexpr double kHopMs = 420;
constexpr double kLongSyllableMs = 700;
constexpr int kIdle = -3;
constexpr int kTitle = -2;
constexpr int kEmpty = -1;
constexpr int kStyleDynamic = 0;
constexpr int kStyleWipe = 1;
constexpr CGFloat kPadding = 16;
constexpr CGFloat kToolbar = 40;
constexpr CGFloat kButtonHeight = 30;
constexpr CGFloat kButtonGap = 4;
constexpr CGFloat kGroupGap = 14;
constexpr CGFloat kEdge = 8;
constexpr CGFloat kButtonFont = 14;
constexpr double kFontStep = 4;
constexpr double kSmallestFont = 8;
constexpr CGFloat kDefaultFont = 40;
constexpr CGFloat kDefaultWidth = 1000;
constexpr CGFloat kBottomMargin = 48;
constexpr CGFloat kDim = 0.45f;
constexpr CGFloat kHop = 10;
constexpr CGFloat kHopScale = 1.12f;
constexpr CGFloat kLongScale = 1.15f;
constexpr CGFloat kOutlineAlpha = 0.7f;
constexpr CGFloat kGlowAlpha = 0.45f;
constexpr CGFloat kBackgroundAlpha = 0.8f;
constexpr CGFloat kBackgroundPadX = 0.4f;
constexpr CGFloat kBackgroundPadY = 0.06f;
constexpr CGFloat kGlowWidth = 0.3f;
constexpr CGFloat kCorner = 4;
constexpr CGFloat kFar = 100000;
constexpr uint32_t kColors[] = {0x3AD38A, 0x3BA7FF, 0xFF6FA8, 0xFFD24A, 0xFFFFFF};
NSString* const kCloseGlyph = @"✕";
const uint32_t kWeightAxis = 'wght';

struct Rgba {
  CGFloat r, g, b, a;
};

Rgba rgb(uint32_t value, CGFloat alpha = 1) {
  return {CGFloat((value >> 16) & 0xFF) / 255, CGFloat((value >> 8) & 0xFF) / 255, CGFloat(value & 0xFF) / 255, alpha};
}

void setFill(CGContextRef context, Rgba c) { CGContextSetRGBFillColor(context, c.r, c.g, c.b, c.a); }
void setStroke(CGContextRef context, Rgba c) { CGContextSetRGBStrokeColor(context, c.r, c.g, c.b, c.a); }

void fillRounded(CGContextRef context, CGRect rect, Rgba color) {
  CGPathRef path = CGPathCreateWithRoundedRect(rect, kCorner, kCorner, nullptr);
  setFill(context, color);
  CGContextAddPath(context, path);
  CGContextFillPath(context);
  CGPathRelease(path);
}

NSString* systemLyricFamily() { return languageId() == L"zh-CN" ? @"PingFang SC" : @"PingFang TC"; }

CGFloat appKitWeight(int weight) {
  if (weight <= 100) return NSFontWeightUltraLight;
  if (weight <= 200) return NSFontWeightThin;
  if (weight <= 300) return NSFontWeightLight;
  if (weight <= 400) return NSFontWeightRegular;
  if (weight <= 500) return NSFontWeightMedium;
  if (weight <= 600) return NSFontWeightSemibold;
  if (weight <= 700) return NSFontWeightBold;
  if (weight <= 800) return NSFontWeightHeavy;
  return NSFontWeightBlack;
}

NSFont* buttonFont() { return [NSFont systemFontOfSize:kButtonFont weight:NSFontWeightBold]; }

CGFloat textWidth(NSString* text) { return [text sizeWithAttributes:@{NSFontAttributeName : buttonFont()}].width; }

void drawCentered(NSString* text, CGRect rect) {
  NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
  paragraph.alignment = NSTextAlignmentCenter;
  NSDictionary* attributes =
    @{NSFontAttributeName : buttonFont(), NSForegroundColorAttributeName : NSColor.whiteColor, NSParagraphStyleAttributeName : paragraph};
  const CGFloat height = [text sizeWithAttributes:attributes].height;
  [text drawInRect:NSMakeRect(rect.origin.x, rect.origin.y + (rect.size.height - height) / 2, rect.size.width, height) withAttributes:attributes];
}

double clockNow() { return NSProcessInfo.processInfo.systemUptime * 1000; }

}

@interface LyricsPanel : NSPanel
@end

@implementation LyricsPanel
- (BOOL)canBecomeKeyWindow {
  return NO;
}
- (BOOL)canBecomeMainWindow {
  return NO;
}
@end

@interface LyricsView : NSView
@property(nonatomic) DesktopLyrics* owner;
@end

@implementation LyricsView {
  NSTrackingArea* _tracking;
  int _resizing;
  CGFloat _lastX;
  BOOL _onButton;
}

- (BOOL)isFlipped {
  return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  if (_tracking) [self removeTrackingArea:_tracking];
  _tracking = [[NSTrackingArea alloc] initWithRect:self.bounds
                                           options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways |
                                                   NSTrackingCursorUpdate
                                             owner:self
                                          userInfo:nil];
  [self addTrackingArea:_tracking];
}

- (NSPoint)pointOf:(NSEvent*)event {
  return [self convertPoint:event.locationInWindow fromView:nil];
}

- (void)cursorUpdate:(NSEvent*)event {
  if (self.owner->edgeAt([self pointOf:event])) [NSCursor.resizeLeftRightCursor set];
  else [NSCursor.arrowCursor set];
}

- (void)mouseMoved:(NSEvent*)event {
  [self cursorUpdate:event];
  self.owner->mouseMoved([self pointOf:event]);
}

- (void)mouseEntered:(NSEvent*)event {
  self.owner->mouseMoved([self pointOf:event]);
}

- (void)mouseExited:(NSEvent*)event {
  self.owner->mouseExited();
}

- (void)mouseDown:(NSEvent*)event {
  const NSPoint point = [self pointOf:event];
  _resizing = self.owner->edgeAt(point);
  _lastX = NSEvent.mouseLocation.x;
  _onButton = !_resizing && self.owner->mouseDown(point);
  if (!_resizing && !_onButton) [self.window performWindowDragWithEvent:event];
}

- (void)mouseDragged:(NSEvent*)event {
  if (!_resizing) return;
  const CGFloat x = NSEvent.mouseLocation.x;
  self.owner->resizeDragged(_resizing < 0 ? _lastX - x : x - _lastX, _resizing < 0);
  _lastX = x;
}

- (void)mouseUp:(NSEvent*)event {
  if (_onButton) self.owner->mouseUp([self pointOf:event]);
  if (_resizing) self.owner->moved();
  _resizing = 0;
  _onButton = NO;
}

- (void)rightMouseDown:(NSEvent*)event {
}

- (void)drawRect:(NSRect)dirty {
  self.owner->draw(NSGraphicsContext.currentContext.CGContext);
}
@end

DesktopLyrics::DesktopLyrics(Command command) : command_(std::move(command)) {
  clockAt_ = clockNow();
  loadSettings();
  panel_ = [[LyricsPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                          styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
  panel_.releasedWhenClosed = NO;
  panel_.opaque = NO;
  panel_.backgroundColor = NSColor.clearColor;
  panel_.hasShadow = NO;
  panel_.level = NSStatusWindowLevel;
  panel_.hidesOnDeactivate = NO;
  panel_.floatingPanel = YES;
  panel_.collectionBehavior =
    NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorStationary;
  view_ = [[LyricsView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
  view_.owner = this;
  panel_.contentView = view_;
  [NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidMoveNotification
                                                  object:panel_
                                                   queue:nil
                                              usingBlock:^(NSNotification*) {
                                                moved();
                                              }];
  placeWindow();
  applyLocked();
  updateLockWatch();
  if (settings_.visible) [panel_ orderFrontRegardless];
  tick();
}

DesktopLyrics::~DesktopLyrics() {
  [frameTimer_ invalidate];
  [lockTimer_ invalidate];
  clearVisuals();
  if (customFont_) CFRelease(customFont_);
}

// #region settings

void DesktopLyrics::loadSettings() {
  NSData* data = [NSData dataWithContentsOfFile:appDataPath(@"desktopLyrics.json")];
  NSDictionary* json = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
  if (![json isKindOfClass:NSDictionary.class]) return;
  auto number = [&](NSString* key, double fallback) { return [json[key] isKindOfClass:NSNumber.class] ? [json[key] doubleValue] : fallback; };
  auto flag = [&](NSString* key, bool fallback) { return [json[key] isKindOfClass:NSNumber.class] ? [json[key] boolValue] : fallback; };
  settings_.x = number(@"x", settings_.x);
  settings_.y = number(@"y", settings_.y);
  settings_.width = number(@"width", settings_.width);
  settings_.fontSize = number(@"fontSize", settings_.fontSize);
  settings_.look.sung = uint32_t(number(@"sung", settings_.look.sung));
  settings_.look.unsung = uint32_t(number(@"unsung", settings_.look.unsung));
  settings_.look.outline = uint32_t(number(@"outline", settings_.look.outline));
  settings_.look.outlineWidth = float(number(@"outlineWidth", settings_.look.outlineWidth));
  settings_.look.weight = int(number(@"weight", settings_.look.weight));
  settings_.look.backgroundOn = flag(@"backgroundOn", settings_.look.backgroundOn);
  settings_.look.background = uint32_t(number(@"background", settings_.look.background));
  settings_.style = int(number(@"style", settings_.style)) == kStyleWipe ? kStyleWipe : kStyleDynamic;
  settings_.visible = flag(@"visible", settings_.visible);
  settings_.locked = flag(@"locked", settings_.locked);
}

void DesktopLyrics::saveSettings() {
  NSDictionary* json = @{
    @"x" : @(std::isnan(settings_.x) ? 0 : settings_.x),
    @"y" : @(std::isnan(settings_.y) ? 0 : settings_.y),
    @"width" : @(settings_.width),
    @"fontSize" : @(settings_.fontSize),
    @"sung" : @(settings_.look.sung),
    @"unsung" : @(settings_.look.unsung),
    @"outline" : @(settings_.look.outline),
    @"outlineWidth" : @(settings_.look.outlineWidth),
    @"weight" : @(settings_.look.weight),
    @"backgroundOn" : @(settings_.look.backgroundOn),
    @"background" : @(settings_.look.background),
    @"style" : @(settings_.style),
    @"visible" : @(settings_.visible),
    @"locked" : @(settings_.locked),
  };
  writeDataFile(appDataPath(@"desktopLyrics.json"), [NSJSONSerialization dataWithJSONObject:json options:0 error:nil]);
}

// #endregion

// #region window

CGFloat DesktopLyrics::lineHeight() const { return settings_.fontSize * 1.45; }
CGFloat DesktopLyrics::windowHeight() const { return std::ceil(kToolbar + 2 * lineHeight() + kPadding / 2); }

CGFloat DesktopLyrics::minimumWidth() const {
  if (buttons_.empty()) return 0;
  return buttons_.back().rect.origin.x + buttons_.back().rect.size.width - buttons_.front().rect.origin.x + 2 * kPadding;
}

void DesktopLyrics::placeWindow() {
  const NSRect work = NSScreen.mainScreen.visibleFrame;
  if (settings_.width <= 0) settings_.width = kDefaultWidth;
  const NSPoint center = NSMakePoint(settings_.x + settings_.width / 2, settings_.y + windowHeight() / 2);
  bool onScreen = false;
  for (NSScreen* screen in NSScreen.screens)
    if (NSPointInRect(center, screen.frame)) onScreen = true;
  if (std::isnan(settings_.x) || std::isnan(settings_.y) || !onScreen) {
    settings_.x = NSMidX(work) - settings_.width / 2;
    settings_.y = NSMinY(work) + kBottomMargin;
  }
  [panel_ setFrame:NSMakeRect(settings_.x, settings_.y, settings_.width, windowHeight()) display:YES];
  layoutButtons();
}

void DesktopLyrics::layoutButtons() {
  struct Spec {
    NSString* action;
    NSString* label;
    bool groupStart;
  };
  const Spec specs[] = {
    {@"restart", ns(tr(Text::LyricsRestart)), false},
    {@"toggle", ns(tr(playing_ ? Text::LyricsPause : Text::LyricsPlay)), false},
    {@"skip", ns(tr(Text::LyricsSkip)), false},
    {@"earlier", ns(tr(Text::LyricsEarlier)), true},
    {@"later", ns(tr(Text::LyricsLater)), false},
    {@"smaller", @"A-", true},
    {@"bigger", @"A+", false},
    {@"color", ns(tr(Text::LyricsColor)), false},
    {@"style", ns(tr(settings_.style == kStyleDynamic ? Text::LyricsDynamic : Text::LyricsWipe)), false},
    {@"lock", ns(tr(Text::LyricsLock)), true},
    {@"close", kCloseGlyph, false},
  };
  buttons_.clear();
  CGFloat x = 0;
  for (const auto& spec : specs) {
    if (spec.groupStart) x += kGroupGap - kButtonGap;
    const CGFloat width = std::max(kButtonHeight, std::ceil(textWidth(spec.label)) + 20);
    const CGFloat top = (kToolbar - kButtonHeight) / 2;
    buttons_.push_back({spec.action, spec.label, CGRectMake(x, top, width, kButtonHeight)});
    x += width + kButtonGap;
  }
  const CGFloat toolbar = x - kButtonGap;
  const CGFloat client = view_.bounds.size.width;
  if (client > 1 && client < toolbar + 2 * kPadding) {
    resizeTo(toolbar + 2 * kPadding);
    return;
  }
  const CGFloat shift = (client - toolbar) / 2;
  for (auto& button : buttons_) button.rect.origin.x += shift;
}

void DesktopLyrics::resizeTo(CGFloat width) {
  NSRect frame = panel_.frame;
  frame.origin.x -= (width - frame.size.width) / 2;
  frame.size.width = width;
  frame.size.height = windowHeight();
  [panel_ setFrame:frame display:NO];
  settings_.x = frame.origin.x;
  settings_.y = frame.origin.y;
  settings_.width = width;
  saveSettings();
  clearVisuals();
  layoutButtons();
  refresh(nowMs(), true);
}

void DesktopLyrics::resizeDragged(CGFloat delta, bool fromLeft) {
  NSRect frame = panel_.frame;
  const CGFloat width = std::max(minimumWidth(), frame.size.width + delta);
  if (width == frame.size.width) return;
  if (fromLeft) frame.origin.x = NSMaxX(frame) - width;
  frame.size.width = width;
  [panel_ setFrame:frame display:NO];
  settings_.width = width;
  clearVisuals();
  layoutButtons();
  refresh(nowMs(), true);
}

void DesktopLyrics::moved() {
  const NSRect frame = panel_.frame;
  settings_.x = frame.origin.x;
  settings_.y = frame.origin.y;
  settings_.width = frame.size.width;
  saveSettings();
}

int DesktopLyrics::edgeAt(NSPoint point) const {
  if (settings_.locked) return 0;
  if (point.x < kEdge) return -1;
  if (point.x >= view_.bounds.size.width - kEdge) return 1;
  return 0;
}

int DesktopLyrics::buttonAt(NSPoint point) const {
  for (size_t i = 0; i < buttons_.size(); ++i)
    if (CGRectContainsPoint(buttons_[i].rect, point)) return int(i);
  return -1;
}

void DesktopLyrics::applyLocked() { panel_.ignoresMouseEvents = settings_.locked && !overUnlock_; }

CGRect DesktopLyrics::unlockRect() const {
  for (const auto& button : buttons_)
    if ([button.action isEqualToString:@"lock"]) return button.rect;
  return CGRectZero;
}

void DesktopLyrics::updateLockWatch() {
  [lockTimer_ invalidate];
  lockTimer_ = nil;
  if (!settings_.visible || !settings_.locked) return;
  lockTimer_ = [NSTimer scheduledTimerWithTimeInterval:kLockPollSeconds
                                               repeats:YES
                                                 block:^(NSTimer*) {
                                                   pollLockHover();
                                                 }];
}

void DesktopLyrics::pollLockHover() {
  const NSPoint cursor = NSEvent.mouseLocation;
  const NSRect frame = panel_.frame;
  const bool inside = NSPointInRect(cursor, frame);
  const NSPoint local = NSMakePoint(cursor.x - frame.origin.x, NSMaxY(frame) - cursor.y);
  const bool over = inside && CGRectContainsPoint(unlockRect(), local);
  if (inside == lockHover_ && over == overUnlock_) return;
  lockHover_ = inside;
  overUnlock_ = over;
  applyLocked();
  refresh(nowMs(), false);
}

void DesktopLyrics::languageChanged() {
  clearVisuals();
  lastFrame_ = @"";
  layoutButtons();
  refresh(nowMs(), true);
}

void DesktopLyrics::setVisible(bool visible) {
  settings_.visible = visible;
  if (visible) [panel_ orderFrontRegardless];
  else [panel_ orderOut:nil];
  saveSettings();
  updateLockWatch();
  lastFrame_ = @"";
  tick();
}

void DesktopLyrics::setLocked(bool locked) {
  settings_.locked = locked;
  hovered_ = false;
  hoveredButton_ = -1;
  lockHover_ = false;
  overUnlock_ = false;
  applyLocked();
  saveSettings();
  updateLockWatch();
  lastFrame_ = @"";
  tick();
}

void DesktopLyrics::press(NSString* action) {
  if ([action isEqualToString:@"smaller"] || [action isEqualToString:@"bigger"]) {
    settings_.fontSize = std::max(kSmallestFont, settings_.fontSize + ([action isEqualToString:@"bigger"] ? kFontStep : -kFontStep));
    resizeTo(panel_.frame.size.width);
    return;
  }
  if ([action isEqualToString:@"color"]) {
    const auto* found = std::find(std::begin(kColors), std::end(kColors), settings_.look.sung);
    settings_.look.sung = found == std::end(kColors) || found + 1 == std::end(kColors) ? kColors[0] : *(found + 1);
    saveSettings();
    tick();
    command_(@"state");
    return;
  }
  if ([action isEqualToString:@"style"]) {
    settings_.style = settings_.style == kStyleDynamic ? kStyleWipe : kStyleDynamic;
    saveSettings();
    layoutButtons();
    tick();
    return;
  }
  if ([action isEqualToString:@"lock"]) {
    setLocked(true);
    command_(@"state");
    return;
  }
  if ([action isEqualToString:@"close"]) {
    setVisible(false);
    command_(@"state");
    return;
  }
  command_(action);
}

void DesktopLyrics::mouseMoved(NSPoint point) {
  if (settings_.locked) return;
  hovered_ = true;
  hoveredButton_ = buttonAt(point);
  refresh(nowMs(), false);
}

void DesktopLyrics::mouseExited() {
  if (NSPointInRect(NSEvent.mouseLocation, panel_.frame)) return;
  hovered_ = false;
  hoveredButton_ = -1;
  refresh(nowMs(), false);
}

bool DesktopLyrics::mouseDown(NSPoint point) {
  if (settings_.locked) return overUnlock_;
  pressedButton_ = hovered_ ? buttonAt(point) : -1;
  return pressedButton_ >= 0;
}

void DesktopLyrics::mouseUp(NSPoint point) {
  if (settings_.locked) {
    if (overUnlock_) {
      setLocked(false);
      command_(@"state");
    }
    return;
  }
  const int index = buttonAt(point);
  if (index >= 0 && index == pressedButton_) press(buttons_[size_t(index)].action);
  pressedButton_ = -1;
}

// #endregion

// #region frames

void DesktopLyrics::clearVisuals() {
  for (auto& [key, visual] : visuals_) {
    if (visual.outline) CGPathRelease(visual.outline);
    for (auto& part : visual.chars)
      if (part.path) CGPathRelease(part.path);
  }
  visuals_.clear();
}

void DesktopLyrics::setSong(NSString* title, NSString* artist, std::vector<LyricLine> lines) {
  hasSong_ = title.length > 0;
  title_ = artist.length ? [NSString stringWithFormat:@"%@　%@", title, artist] : title;
  lines_ = std::move(lines);
  clearVisuals();
  slotKeys_[0] = slotKeys_[1] = INT_MIN;
  tick();
}

void DesktopLyrics::setLook(const LyricLook& look, bool save) {
  if (look.weight != settings_.look.weight) clearVisuals();
  settings_.look = look;
  if (save) saveSettings();
  tick();
}

void DesktopLyrics::setFont(NSString* path) {
  CTFontDescriptorRef descriptor = nullptr;
  if (path.length) {
    CFArrayRef found = CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)[NSURL fileURLWithPath:path]);
    if (!found || CFArrayGetCount(found) == 0) {
      if (found) CFRelease(found);
      throw HostError([@"字檔讀不出來：" stringByAppendingString:path]);
    }
    descriptor = static_cast<CTFontDescriptorRef>(CFRetain(CFArrayGetValueAtIndex(found, 0)));
    CFRelease(found);
  }
  if (customFont_) CFRelease(customFont_);
  customFont_ = descriptor;
  clearVisuals();
  lastFrame_ = @"";
  tick();
}

CTFontRef DesktopLyrics::lyricFont(CGFloat size) const {
  if (customFont_) {
    NSDictionary* variation = @{@(kWeightAxis) : @(settings_.look.weight)};
    CTFontDescriptorRef weighted =
      CTFontDescriptorCreateCopyWithAttributes(customFont_, (__bridge CFDictionaryRef) @{(id)kCTFontVariationAttribute : variation});
    CTFontRef font = CTFontCreateWithFontDescriptor(weighted, size, nullptr);
    CFRelease(weighted);
    return font;
  }
  NSFontDescriptor* descriptor = [NSFontDescriptor fontDescriptorWithFontAttributes:@{
    NSFontFamilyAttribute : systemLyricFamily(),
    NSFontTraitsAttribute : @{NSFontWeightTrait : @(appKitWeight(settings_.look.weight))},
  }];
  return CTFontCreateWithFontDescriptor((__bridge CTFontDescriptorRef)descriptor, size, nullptr);
}

void DesktopLyrics::setClock(double ms, bool playing) {
  clockMs_ = ms;
  clockAt_ = clockNow();
  if (playing != playing_) {
    playing_ = playing;
    layoutButtons();
  }
  tick();
}

double DesktopLyrics::nowMs() const { return playing_ ? clockMs_ + clockNow() - clockAt_ : clockMs_; }

void DesktopLyrics::tick() {
  [frameTimer_ invalidate];
  frameTimer_ = nil;
  if (!settings_.visible) return;
  const double ms = nowMs();
  const bool moving = refresh(ms, false);
  if (!playing_ && !moving) return;
  const double delay = moving ? kFrameMs : nextChangeAt(ms) - ms;
  if (!std::isfinite(delay)) return;
  frameTimer_ = [NSTimer scheduledTimerWithTimeInterval:std::max(1.0, delay) / 1000
                                                repeats:NO
                                                  block:^(NSTimer*) {
                                                    tick();
                                                  }];
}

int DesktopLyrics::lineAt(double ms) const {
  int index = -1;
  for (int i = 0; i < int(lines_.size()); ++i)
    if (lines_[size_t(i)].start <= ms) index = i;
  return index;
}

double DesktopLyrics::nextChangeAt(double ms) const {
  double next = std::numeric_limits<double>::infinity();
  for (const auto& line : lines_) {
    if (line.start > ms) {
      next = std::min(next, line.start);
      break;
    }
  }
  const int current = lineAt(ms);
  if (current >= 0) {
    for (const auto& c : lines_[size_t(current)].chars) {
      if (c.t > ms) {
        next = std::min(next, c.t);
        break;
      }
    }
  }
  return next;
}

void DesktopLyrics::slotKeysAt(double ms, int keys[2]) const {
  keys[0] = keys[1] = kEmpty;
  if (!hasSong_) {
    keys[0] = kIdle;
    return;
  }
  if (lines_.empty()) {
    keys[0] = kTitle;
    return;
  }
  const int current = lineAt(ms);
  if (current < 0) {
    keys[0] = kTitle;
    keys[1] = 0;
    return;
  }
  keys[current % 2] = current;
  keys[(current + 1) % 2] = current + 1 < int(lines_.size()) ? current + 1 : kEmpty;
}

const DesktopLyrics::Visual& DesktopLyrics::visualFor(int key, NSString* text, const std::vector<LyricChar>& chars) {
  auto found = visuals_.find(key);
  if (found != visuals_.end()) return found->second;
  Visual& visual = visuals_[key];
  const CGFloat room = view_.bounds.size.width - 2 * kPadding;
  if (!text.length || room <= 0) return visual;

  CGFloat size = settings_.fontSize;
  CTLineRef line = nullptr;
  CGFloat ascent = 0;
  CGFloat descent = 0;
  CGFloat width = 0;
  CTFontRef font = nullptr;
  auto build = [&](CGFloat fontSize) {
    if (line) CFRelease(line);
    if (font) CFRelease(font);
    font = lyricFont(fontSize);
    NSAttributedString* attributed = [[NSAttributedString alloc] initWithString:text attributes:@{(id)kCTFontAttributeName : (__bridge id)font}];
    line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)attributed);
    width = CGFloat(CTLineGetTypographicBounds(line, &ascent, &descent, nullptr));
  };
  build(size);
  if (width > room) {
    size = size * room / width;
    build(size);
  }

  struct Part {
    NSUInteger index;
    CGPathRef path;
  };
  std::vector<Part> parts;
  CFArrayRef runs = CTLineGetGlyphRuns(line);
  for (CFIndex r = 0; r < CFArrayGetCount(runs); ++r) {
    CTRunRef run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs, r));
    CTFontRef runFont = static_cast<CTFontRef>(CFDictionaryGetValue(CTRunGetAttributes(run), kCTFontAttributeName));
    const CFIndex count = CTRunGetGlyphCount(run);
    std::vector<CGGlyph> glyphs(static_cast<size_t>(count));
    std::vector<CGPoint> positions(static_cast<size_t>(count));
    std::vector<CFIndex> indices(static_cast<size_t>(count));
    CTRunGetGlyphs(run, CFRangeMake(0, 0), glyphs.data());
    CTRunGetPositions(run, CFRangeMake(0, 0), positions.data());
    CTRunGetStringIndices(run, CFRangeMake(0, 0), indices.data());
    for (CFIndex g = 0; g < count; ++g) {
      const CGAffineTransform place = CGAffineTransformMake(1, 0, 0, -1, positions[size_t(g)].x, ascent - positions[size_t(g)].y);
      CGPathRef path = CTFontCreatePathForGlyph(runFont, glyphs[size_t(g)], &place);
      if (path) parts.push_back({NSUInteger(indices[size_t(g)]), path});
    }
  }
  auto group = [&](NSUInteger from, NSUInteger to) -> CGPathRef {
    CGMutablePathRef combined = CGPathCreateMutable();
    for (const auto& part : parts)
      if (part.index >= from && part.index < to) CGPathAddPath(combined, nullptr, part.path);
    if (CGPathIsEmpty(combined)) {
      CGPathRelease(combined);
      return nullptr;
    }
    return combined;
  };
  visual.outline = group(0, NSUIntegerMax);
  visual.size = size;
  visual.width = width;
  visual.height = ascent + descent;
  for (const auto& c : chars) {
    const CGFloat left = CTLineGetOffsetForStringIndex(line, CFIndex(c.pos), nullptr);
    const CGFloat right = CTLineGetOffsetForStringIndex(line, CFIndex(c.pos + c.len), nullptr);
    visual.chars.push_back({group(c.pos, c.pos + c.len), CGRectMake(std::min(left, right), 0, std::abs(right - left), visual.height)});
  }
  for (const auto& part : parts) CGPathRelease(part.path);
  CFRelease(line);
  CFRelease(font);
  return visual;
}

bool DesktopLyrics::refresh(double ms, bool force) {
  if (!settings_.visible) return false;
  const double wall = clockNow();
  int keys[2];
  slotKeysAt(ms, keys);
  for (int i = 0; i < 2; ++i) {
    if (keys[i] != slotKeys_[i]) {
      slotSince_[i] = slotKeys_[i] == INT_MIN ? wall - kFadeMs : wall;
      slotKeys_[i] = keys[i];
    }
  }
  const int current = lines_.empty() ? -1 : lineAt(ms);
  const bool dynamic = settings_.style == kStyleDynamic;
  bool moving = false;
  NSMutableString* frame = [NSMutableString string];
  for (int i = 0; i < 2; ++i) {
    const double fade = std::min(1.0, (wall - slotSince_[i]) / kFadeMs);
    if (fade < 1) moving = true;
    [frame appendFormat:@"%d:%ld|", keys[i], std::lround(fade * 20)];
  }
  if (current >= 0) {
    for (const auto& c : lines_[size_t(current)].chars) {
      const double p = std::clamp((ms - c.t) / std::max(1.0, c.e - c.t), 0.0, 1.0);
      const double hop = std::clamp((ms - c.t) / kHopMs, 0.0, 1.0);
      if (playing_ && ((p > 0 && p < 1) || (dynamic && hop > 0 && hop < 1))) moving = true;
      [frame appendFormat:@"%ld.%ld,", std::lround(p * 40), std::lround(hop * 20)];
    }
  }
  const LyricLook& look = settings_.look;
  [frame appendFormat:@"|%d%d%d%u,%u,%u,%g,%d,%d,%u,%d%d%d%d|%gx%g", hovered_, hoveredButton_, playing_, look.sung, look.unsung, look.outline,
                      look.outlineWidth, look.weight, look.backgroundOn, look.background, settings_.style, settings_.locked, lockHover_,
                      overUnlock_, view_.bounds.size.width, view_.bounds.size.height];
  if (!force && [frame isEqualToString:lastFrame_]) return moving;
  lastFrame_ = frame;
  view_.needsDisplay = YES;
  return moving;
}

void DesktopLyrics::draw(CGContextRef context) {
  const CGRect all = view_.bounds;
  CGContextClearRect(context, all);
  const double ms = nowMs();
  const double wall = clockNow();
  int keys[2] = {slotKeys_[0], slotKeys_[1]};
  const int current = lines_.empty() ? -1 : lineAt(ms);
  const bool dynamic = settings_.style == kStyleDynamic;

  if (!settings_.locked) fillRounded(context, all, {0, 0, 0, hovered_ ? 0.42 : 1.0 / 255});
  if (settings_.locked && lockHover_) {
    const CGRect button = unlockRect();
    fillRounded(context, button, {0, 0, 0, overUnlock_ ? 0.7 : 0.5});
    if (overUnlock_) fillRounded(context, button, {1, 1, 1, 0.18});
    drawCentered(ns(tr(Text::LyricsUnlock)), button);
  }
  if (hovered_ && !settings_.locked) {
    for (size_t i = 0; i < buttons_.size(); ++i) {
      if (int(i) == hoveredButton_) fillRounded(context, buttons_[i].rect, {1, 1, 1, 0.18});
      drawCentered(buttons_[i].label, buttons_[i].rect);
    }
  }

  const Rgba sung = rgb(settings_.look.sung);
  const Rgba unsung = rgb(settings_.look.unsung);
  const Rgba edge = rgb(settings_.look.outline);
  auto mix = [](Rgba a, Rgba b, CGFloat t, CGFloat alpha) {
    return Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, alpha};
  };
  CGContextSetLineJoin(context, kCGLineJoinRound);
  CGContextSetLineCap(context, kCGLineCapRound);
  auto stroke = [&](CGPathRef path, CGFloat width, Rgba color) {
    setStroke(context, color);
    CGContextSetLineWidth(context, width);
    CGContextAddPath(context, path);
    CGContextStrokePath(context);
  };
  auto fill = [&](CGPathRef path, Rgba color) {
    setFill(context, color);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
  };
  auto paint = [&](CGPathRef path, CGFloat size, Rgba color, CGFloat glow) {
    if (glow > 0) stroke(path, size * kGlowWidth, {color.r, color.g, color.b, kGlowAlpha * glow * color.a});
    if (settings_.look.outlineWidth > 0)
      stroke(path, size * settings_.look.outlineWidth, {edge.r, edge.g, edge.b, kOutlineAlpha * std::min<CGFloat>(1, color.a * 1.8)});
    fill(path, color);
  };

  const CGFloat lineH = lineHeight();
  for (int i = 0; i < 2; ++i) {
    const int key = keys[i];
    if (key == kEmpty) continue;
    const bool isLine = key >= 0;
    static const std::vector<LyricChar> none;
    const Visual& visual = isLine ? visualFor(key, lines_[size_t(key)].text, lines_[size_t(key)].chars)
                                  : visualFor(key, key == kTitle ? title_ : ns(std::wstring(APP_NAME)), none);
    if (!visual.outline) continue;
    const CGFloat fade = CGFloat(std::min(1.0, (wall - slotSince_[i]) / kFadeMs));
    const CGFloat x = i == 1 ? all.size.width - kPadding - visual.width : kPadding;
    const CGFloat y = kToolbar + lineH * i + (lineH - visual.height) / 2;

    if (settings_.look.backgroundOn) {
      const CGFloat padX = visual.size * kBackgroundPadX;
      const CGFloat padY = visual.size * kBackgroundPadY;
      fillRounded(context, CGRectMake(x - padX, y - padY, visual.width + 2 * padX, visual.height + 2 * padY),
                  rgb(settings_.look.background, kBackgroundAlpha * fade));
    }

    CGContextSaveGState(context);
    CGContextTranslateCTM(context, x, y);
    if (!isLine || key != current) {
      paint(visual.outline, visual.size, {unsung.r, unsung.g, unsung.b, (dynamic && isLine ? kDim : 1) * fade}, 0);
      CGContextRestoreGState(context);
      continue;
    }

    const LyricLine& line = lines_[size_t(key)];
    if (!dynamic) {
      CGFloat filled = 0;
      for (size_t k = 0; k < line.chars.size(); ++k) {
        const LyricChar& c = line.chars[k];
        if (c.t > ms) break;
        const CGFloat p = CGFloat(std::clamp((ms - c.t) / std::max(1.0, c.e - c.t), 0.0, 1.0));
        const CGRect& box = visual.chars[k].box;
        filled = box.origin.x + box.size.width * p;
      }
      if (settings_.look.outlineWidth > 0) stroke(visual.outline, visual.size * settings_.look.outlineWidth, {edge.r, edge.g, edge.b, kOutlineAlpha});
      CGContextSaveGState(context);
      CGContextClipToRect(context, CGRectMake(-kFar, -kFar, filled + kFar, 2 * kFar));
      fill(visual.outline, sung);
      CGContextRestoreGState(context);
      CGContextSaveGState(context);
      CGContextClipToRect(context, CGRectMake(filled, -kFar, 2 * kFar, 2 * kFar));
      fill(visual.outline, unsung);
      CGContextRestoreGState(context);
      CGContextRestoreGState(context);
      continue;
    }

    for (size_t k = 0; k < line.chars.size(); ++k) {
      const LyricChar& c = line.chars[k];
      const auto& part = visual.chars[k];
      if (!part.path) continue;
      const double duration = std::max(1.0, c.e - c.t);
      const CGFloat p = CGFloat(std::clamp((ms - c.t) / duration, 0.0, 1.0));
      const CGFloat hop = CGFloat(std::clamp((ms - c.t) / kHopMs, 0.0, 1.0));
      const CGFloat bounce = hop > 0 && hop < 1 ? std::sin(M_PI * hop) : 0;
      const CGFloat emphasis = duration >= kLongSyllableMs && p > 0 && p < 1 ? std::sin(M_PI * p) : 0;
      const CGFloat grow = 1 + (kHopScale - 1) * bounce + (kLongScale - 1) * emphasis;
      const CGFloat centerX = CGRectGetMidX(part.box);
      const CGFloat bottom = CGRectGetMaxY(part.box);
      CGContextSaveGState(context);
      CGContextTranslateCTM(context, 0, -kHop * visual.size / kDefaultFont * bounce);
      CGContextTranslateCTM(context, centerX, bottom);
      CGContextScaleCTM(context, grow, grow);
      CGContextTranslateCTM(context, -centerX, -bottom);
      paint(part.path, visual.size, mix(unsung, sung, p, kDim + (1 - kDim) * p), emphasis);
      CGContextRestoreGState(context);
    }
    CGContextRestoreGState(context);
  }
}

// #endregion
