#include "nowPlaying.h"

#include <systemmediatransportcontrolsinterop.h>

#include <chrono>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include "files.h"

using namespace winrt::Windows::Media;
using winrt::Windows::Foundation::TimeSpan;
using winrt::Windows::Foundation::Uri;
using winrt::Windows::Storage::Streams::RandomAccessStreamReference;

namespace {

constexpr wchar_t kLyricsFile[] = L"\\lyrics.json";

TimeSpan spanOf(double ms) { return std::chrono::duration_cast<TimeSpan>(std::chrono::duration<double, std::milli>(ms)); }

MediaPlaybackStatus statusOf(const std::wstring& state) {
  if (state == L"playing") return MediaPlaybackStatus::Playing;
  if (state == L"paused") return MediaPlaybackStatus::Paused;
  if (state == L"loading") return MediaPlaybackStatus::Changing;
  if (state == L"idle") return MediaPlaybackStatus::Stopped;
  throw winrt::hresult_invalid_argument(L"媒體控制不認得的播放狀態：" + state);
}

}

NowPlaying::NowPlaying(HWND window, Control control) {
  auto interop = winrt::get_activation_factory<SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
  winrt::check_hresult(interop->GetForWindow(window, winrt::guid_of<SystemMediaTransportControls>(), winrt::put_abi(controls_)));
  controls_.IsPlayEnabled(true);
  controls_.IsPauseEnabled(true);
  controls_.IsNextEnabled(true);
  controls_.PlaybackStatus(MediaPlaybackStatus::Stopped);
  controls_.IsEnabled(true);
  controls_.ButtonPressed([control](const SystemMediaTransportControls&, const SystemMediaTransportControlsButtonPressedEventArgs& args) {
    switch (args.Button()) {
      case SystemMediaTransportControlsButton::Play:
        control(L"play");
        break;
      case SystemMediaTransportControlsButton::Pause:
        control(L"pause");
        break;
      case SystemMediaTransportControlsButton::Next:
        control(L"skip");
        break;
      default:
        break;
    }
  });
}

void NowPlaying::setTrack(const NowPlayingTrack& track) {
  auto updater = controls_.DisplayUpdater();
  updater.Type(MediaPlaybackType::Music);
  auto music = updater.MusicProperties();
  music.Title(track.title);
  music.Artist(track.artist);
  music.AlbumTitle(track.album);
  updater.Thumbnail(track.cover.empty() ? nullptr : RandomAccessStreamReference::CreateFromUri(Uri(track.cover)));
  updater.Update();
}

void NowPlaying::clearTrack() {
  controls_.DisplayUpdater().ClearAll();
  controls_.DisplayUpdater().Update();
}

void NowPlaying::setPlayback(const std::wstring& state, double positionMs, double durationMs) {
  controls_.PlaybackStatus(statusOf(state));
  SystemMediaTransportControlsTimelineProperties timeline;
  timeline.StartTime(TimeSpan::zero());
  timeline.MinSeekTime(TimeSpan::zero());
  timeline.Position(spanOf(positionMs));
  timeline.MaxSeekTime(spanOf(durationMs));
  timeline.EndTime(spanOf(durationMs));
  controls_.UpdateTimelineProperties(timeline);
}

void NowPlaying::setLyrics(const std::wstring& json) { writeUtf8File(appDataDir() + kLyricsFile, json); }
