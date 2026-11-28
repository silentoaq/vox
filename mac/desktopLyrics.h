#pragma once
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

struct LyricChar {
  double t;
  double e;
  NSUInteger pos;
  NSUInteger len;
};

struct LyricLine {
  double start;
  NSString* text;
  std::vector<LyricChar> chars;
};

struct LyricLook {
  uint32_t sung = 0x3AD38A;
  uint32_t unsung = 0xFFFFFF;
  uint32_t outline = 0x000000;
  float outlineWidth = 0.12f;
  int weight = 700;
  bool backgroundOn = false;
  uint32_t background = 0x101012;
};

@class LyricsView;

class DesktopLyrics {
public:
  using Command = std::function<void(NSString* action)>;

  explicit DesktopLyrics(Command command);
  ~DesktopLyrics();

  bool visible() const { return settings_.visible; }
  bool locked() const { return settings_.locked; }
  const LyricLook& look() const { return settings_.look; }
  void setLook(const LyricLook& look, bool save);
  void setVisible(bool visible);
  void languageChanged();
  void setLocked(bool locked);
  void setSong(NSString* title, NSString* artist, std::vector<LyricLine> lines);
  void setFont(NSString* path);
  void setClock(double ms, bool playing);

  void draw(CGContextRef context);
  void mouseMoved(NSPoint point);
  void mouseExited();
  bool mouseDown(NSPoint point);
  void mouseUp(NSPoint point);
  void resizeDragged(CGFloat delta, bool fromLeft);
  int edgeAt(NSPoint point) const;
  void moved();

private:
  struct Settings {
    double x = NAN;
    double y = NAN;
    double width = 0;
    double fontSize = 40;
    LyricLook look;
    int style = 0;
    bool visible = false;
    bool locked = false;
  };

  struct CharVisual {
    CGPathRef path = nullptr;
    CGRect box = CGRectZero;
  };

  struct Visual {
    CGFloat size = 0;
    CGFloat width = 0;
    CGFloat height = 0;
    CGPathRef outline = nullptr;
    std::vector<CharVisual> chars;
  };

  struct Button {
    NSString* action;
    NSString* label;
    CGRect rect;
  };

  void loadSettings();
  void saveSettings();
  void applyLocked();
  void updateLockWatch();
  void pollLockHover();
  CGRect unlockRect() const;
  void placeWindow();
  CGFloat lineHeight() const;
  CGFloat windowHeight() const;
  CGFloat minimumWidth() const;
  void layoutButtons();
  int buttonAt(NSPoint point) const;
  void press(NSString* action);
  void resizeTo(CGFloat width);
  void clearVisuals();
  CTFontRef lyricFont(CGFloat size) const;

  double nowMs() const;
  void tick();
  double nextChangeAt(double ms) const;
  int lineAt(double ms) const;
  void slotKeysAt(double ms, int keys[2]) const;
  bool refresh(double ms, bool force);
  const Visual& visualFor(int key, NSString* text, const std::vector<LyricChar>& chars);

  Command command_;
  Settings settings_;
  NSPanel* panel_ = nil;
  LyricsView* view_ = nil;
  NSTimer* frameTimer_ = nil;
  NSTimer* lockTimer_ = nil;
  NSString* title_ = @"";
  std::vector<LyricLine> lines_;
  double clockMs_ = 0;
  double clockAt_ = 0;
  bool playing_ = false;
  bool hasSong_ = false;
  bool hovered_ = false;
  int hoveredButton_ = -1;
  int pressedButton_ = -1;
  bool lockHover_ = false;
  bool overUnlock_ = false;
  int slotKeys_[2] = {INT_MIN, INT_MIN};
  double slotSince_[2] = {0, 0};
  std::vector<Button> buttons_;
  std::map<int, Visual> visuals_;
  NSString* lastFrame_ = @"";
  CTFontDescriptorRef customFont_ = nullptr;
};
