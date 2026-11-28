#pragma once
#import <Foundation/Foundation.h>

#include <functional>

struct UpdateStatus {
  NSString* state;
  NSString* version;
  NSString* message;
};

bool launchStagedUpdate();

class Updater {
public:
  using Notify = std::function<void(const UpdateStatus&)>;
  explicit Updater(Notify notify);
  void started();

private:
  Notify notify_;
  bool checked_ = false;
};
