#pragma once
#import <Foundation/Foundation.h>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

struct KaraokeStatus {
  NSString* id;
  NSString* state;
  int segment = -1;
  int total = 0;
  int stride = 0;
  int samples = 0;
  NSString* message = @"";
  double progress = 0;
};

class Karaoke {
public:
  using Report = std::function<void(const KaraokeStatus& status)>;

  explicit Karaoke(Report report);
  ~Karaoke();
  static NSString* stemsDir();
  void prepare(NSString* id, double fromSeconds, bool urgent, NSString* cookies);

private:
  struct Job {
    NSString* id;
    double from;
    NSString* cookies;
    bool urgent;
  };
  struct Running {
    std::mutex lock;
    pid_t process = 0;
    pid_t download = 0;
    bool cancelled = false;
  };

  void startNext();
  void cancel();
  void run(Job job);
  bool ensureYtDlp(NSString* path, NSString* id);
  void post(KaraokeStatus status);
  void finish(const KaraokeStatus& status);

  Report report_;
  std::deque<Job> queue_;
  std::optional<Job> running_;
  std::shared_ptr<Running> state_ = std::make_shared<Running>();
};
