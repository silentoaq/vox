#pragma once
#import <Foundation/Foundation.h>

#include <functional>

class NowPlaying {
public:
  using Control = std::function<void(NSString* action)>;

  explicit NowPlaying(Control control);
  void setTrack(NSString* title, NSString* artist, NSString* album, NSString* cover);
  void clearTrack();
  void setPlayback(NSString* state, double positionMs, double durationMs);
  void setLyrics(NSString* json);

private:
  Control control_;
  NSMutableDictionary* info_ = [NSMutableDictionary dictionary];
  unsigned coverSerial_ = 0;
};
