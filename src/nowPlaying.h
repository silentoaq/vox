#pragma once
#include <windows.h>

#include <functional>
#include <string>

#include <winrt/Windows.Media.h>

struct NowPlayingTrack {
  std::wstring title;
  std::wstring artist;
  std::wstring album;
  std::wstring cover;
};

class NowPlaying {
public:
  using Control = std::function<void(const std::wstring& action)>;

  NowPlaying(HWND window, Control control);
  void setTrack(const NowPlayingTrack& track);
  void clearTrack();
  void setPlayback(const std::wstring& state, double positionMs, double durationMs);
  void setLyrics(const std::wstring& json);

private:
  winrt::Windows::Media::SystemMediaTransportControls controls_{nullptr};
};
