#import "nowPlaying.h"

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

#import "common.h"
#import "files.h"
#import "http.h"

namespace {

MPNowPlayingPlaybackState stateOf(NSString* state) {
  if ([state isEqualToString:@"playing"]) return MPNowPlayingPlaybackStatePlaying;
  if ([state isEqualToString:@"paused"]) return MPNowPlayingPlaybackStatePaused;
  if ([state isEqualToString:@"loading"]) return MPNowPlayingPlaybackStateInterrupted;
  if ([state isEqualToString:@"idle"]) return MPNowPlayingPlaybackStateStopped;
  throw HostError([@"媒體控制不認得的播放狀態：" stringByAppendingString:state]);
}

}

NowPlaying::NowPlaying(Control control) : control_(std::move(control)) {
  MPRemoteCommandCenter* center = MPRemoteCommandCenter.sharedCommandCenter;
  auto bind = [this](MPRemoteCommand* command, NSString* action) {
    command.enabled = YES;
    [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
      control_(action);
      return MPRemoteCommandHandlerStatusSuccess;
    }];
  };
  bind(center.playCommand, @"play");
  bind(center.pauseCommand, @"pause");
  bind(center.togglePlayPauseCommand, @"toggle");
  bind(center.nextTrackCommand, @"skip");
  MPNowPlayingInfoCenter.defaultCenter.playbackState = MPNowPlayingPlaybackStateStopped;
}

void NowPlaying::setTrack(NSString* title, NSString* artist, NSString* album, NSString* cover) {
  info_[MPMediaItemPropertyTitle] = title;
  info_[MPMediaItemPropertyArtist] = artist;
  info_[MPMediaItemPropertyAlbumTitle] = album;
  info_[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);
  [info_ removeObjectForKey:MPMediaItemPropertyArtwork];
  MPNowPlayingInfoCenter.defaultCenter.nowPlayingInfo = info_;
  const unsigned serial = ++coverSerial_;
  if (!cover.length) return;
  httpFetch(httpRequest(cover), ^(HttpResult result) {
    NSImage* image = result.status == 200 ? [[NSImage alloc] initWithData:result.body] : nil;
    if (!image) return;
    onMain(^{
      if (serial != coverSerial_) return;
      info_[MPMediaItemPropertyArtwork] = [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
                                                                         requestHandler:^NSImage*(CGSize) {
                                                                           return image;
                                                                         }];
      MPNowPlayingInfoCenter.defaultCenter.nowPlayingInfo = info_;
    });
  });
}

void NowPlaying::clearTrack() {
  ++coverSerial_;
  [info_ removeAllObjects];
  MPNowPlayingInfoCenter.defaultCenter.nowPlayingInfo = nil;
}

void NowPlaying::setPlayback(NSString* state, double positionMs, double durationMs) {
  MPNowPlayingInfoCenter.defaultCenter.playbackState = stateOf(state);
  if (!info_.count) return;
  info_[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(positionMs / 1000);
  info_[MPMediaItemPropertyPlaybackDuration] = @(durationMs / 1000);
  info_[MPNowPlayingInfoPropertyPlaybackRate] = @([state isEqualToString:@"playing"] ? 1.0 : 0.0);
  MPNowPlayingInfoCenter.defaultCenter.nowPlayingInfo = info_;
}

void NowPlaying::setLyrics(NSString* json) { writeTextFile(appDataPath(@"lyrics.json"), json); }
