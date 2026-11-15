#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>

struct UpdateStatus {
  std::wstring state;
  std::wstring version;
  std::wstring message;
};

class Updater {
public:
  using Notify = std::function<void(const UpdateStatus&)>;
  explicit Updater(Notify notify);
  ~Updater();
  Updater(const Updater&) = delete;
  Updater& operator=(const Updater&) = delete;
  void started();

  struct Shared;

private:
  std::shared_ptr<Shared> shared_;
  std::optional<std::wstring> root_;
  bool checked_ = false;
};
