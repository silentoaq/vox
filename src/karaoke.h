#pragma once
#include <windows.h>

#include <deque>
#include <functional>
#include <optional>
#include <string>

inline constexpr UINT WM_APP_KARAOKE = WM_APP + 20;

struct KaraokeStatus {
  std::wstring id;
  std::wstring state;
  int segment = -1;
  int total = 0;
  int stride = 0;
  int samples = 0;
  std::wstring message;
  double progress = 0;
};

class Karaoke {
public:
  using Report = std::function<void(const KaraokeStatus& status)>;

  Karaoke(HWND hwnd, Report report);
  ~Karaoke();

  static std::wstring stemsDir();
  void prepare(const std::wstring& id, double fromSeconds, bool urgent, std::string cookies);
  void finish(LPARAM status);

private:
  struct Job {
    std::wstring id;
    double from;
    std::string cookies;
    bool urgent;
  };

  void startNext();
  void cancel();
  void run(Job job);
  bool ensureYtDlp(const std::wstring& dir, const std::wstring& id);
  void post(KaraokeStatus status);

  HWND hwnd_;
  Report report_;
  std::deque<Job> queue_;
  std::optional<Job> running_;
  HANDLE process_ = nullptr;
  HANDLE download_ = nullptr;
  bool cancelled_ = false;
};
