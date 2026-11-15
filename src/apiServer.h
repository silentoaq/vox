#pragma once
#include <functional>
#include <memory>
#include <string>

inline constexpr unsigned short kApiPort = 8690;

struct ApiReply {
  int status;
  std::string body;
};

class ApiServer {
public:
  using Reply = std::function<void(ApiReply)>;
  using Forward = std::function<void(const std::string& kind, const std::string& body, Reply reply)>;

  explicit ApiServer(Forward forward);
  ~ApiServer();
  ApiServer(const ApiServer&) = delete;
  ApiServer& operator=(const ApiServer&) = delete;

  std::wstring start();
  void publish(const std::string& event, const std::string& json);

  struct Impl;

private:
  std::shared_ptr<Impl> impl_;
};
