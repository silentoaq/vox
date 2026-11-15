#pragma warning(push, 0)
#include <httplib.h>
#pragma warning(pop)

#include "apiServer.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <thread>

#include <winrt/base.h>

namespace {

constexpr std::array<std::string_view, 5> kEvents = {"track", "playback", "lyrics", "line", "queue"};
constexpr std::array<std::string_view, 4> kStateEvents = {"track", "playback", "line", "queue"};
constexpr auto kHeartbeat = std::chrono::seconds(15);
constexpr char kJsonType[] = "application/json; charset=utf-8";
const std::string kHeartbeatFrame = ": \n\n";

std::string jsonString(std::string_view text) {
  std::string out = "\"";
  for (char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[8];
          snprintf(escaped, sizeof escaped, "\\u%04x", c);
          out += escaped;
        } else {
          out += c;
        }
    }
  }
  return out + "\"";
}

void sendError(httplib::Response& res, int status, std::string_view message) {
  res.status = status;
  res.set_content("{\"error\":" + jsonString(message) + "}", kJsonType);
}

class ConnectionThreads final : public httplib::TaskQueue {
public:
  bool enqueue(std::function<void()> fn) override {
    std::lock_guard lock(mutex_);
    try {
      std::thread([this, fn = std::move(fn)] {
        fn();
        std::lock_guard done(mutex_);
        if (--running_ == 0) idle_.notify_all();
      }).detach();
    } catch (const std::system_error&) {
      return false;
    }
    ++running_;
    return true;
  }

  void shutdown() override {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return running_ == 0; });
  }

private:
  std::mutex mutex_;
  std::condition_variable idle_;
  size_t running_ = 0;
};

}

struct ApiServer::Impl {
  struct Subscriber {
    std::deque<std::string> frames;
  };

  explicit Impl(Forward f) : forward(std::move(f)) {}

  std::optional<std::string> snapshot(std::string_view event) const {
    auto found = latest.find(std::string(event));
    if (found == latest.end()) return std::nullopt;
    return found->second;
  }

  Forward forward;
  httplib::Server server;
  std::thread listener;
  std::mutex mutex;
  std::condition_variable changed;
  bool closing = false;
  std::map<std::string, std::string> latest;
  std::set<std::shared_ptr<Subscriber>> subscribers;
};

namespace {

std::string frameOf(std::string_view event, std::string_view json) {
  return "event: " + std::string(event) + "\ndata: " + std::string(json) + "\n\n";
}

void forwardAndWait(const std::shared_ptr<ApiServer::Impl>& impl, const char* kind, const httplib::Request& req,
                    httplib::Response& res) {
  auto reply = std::make_shared<std::optional<ApiReply>>();
  std::weak_ptr<ApiServer::Impl> weak = impl;
  impl->forward(kind, req.body, [weak, reply](ApiReply answer) {
    auto owner = weak.lock();
    if (!owner) return;
    std::lock_guard lock(owner->mutex);
    *reply = std::move(answer);
    owner->changed.notify_all();
  });
  std::unique_lock lock(impl->mutex);
  impl->changed.wait(lock, [&] { return reply->has_value() || impl->closing; });
  if (!reply->has_value()) {
    lock.unlock();
    sendError(res, 503, "Vox 正在關閉");
    return;
  }
  ApiReply answer = std::move(**reply);
  lock.unlock();
  res.status = answer.status;
  res.set_content(answer.body, kJsonType);
}

}

ApiServer::ApiServer(Forward forward) : impl_(std::make_shared<Impl>(std::move(forward))) {
  Impl* impl = impl_.get();
  std::weak_ptr<Impl> weak = impl_;
  httplib::Server& server = impl->server;
  const std::string port = std::to_string(kApiPort);
  const std::set<std::string> hosts = {"127.0.0.1:" + port, "localhost:" + port};

  server.new_task_queue = [] { return new ConnectionThreads; };
  server.set_socket_options([](SOCKET sock) {
    BOOL exclusive = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);
  });

  server.set_pre_routing_handler([hosts](const httplib::Request& req, httplib::Response& res) {
    if (!hosts.contains(req.get_header_value("Host"))) {
      sendError(res, 403, "只接受連到 127.0.0.1 或 localhost 的請求");
      return httplib::Server::HandlerResponse::Handled;
    }
    const std::string site = req.get_header_value("Sec-Fetch-Site");
    if (req.has_header("Origin") || (!site.empty() && site != "none")) {
      sendError(res, 403, "不接受網頁發出的請求");
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
    if (!res.body.empty()) return httplib::Server::HandlerResponse::Unhandled;
    sendError(res, res.status, res.status == 404 ? "沒有這個路徑" : httplib::status_message(res.status));
    return httplib::Server::HandlerResponse::Handled;
  });

  server.Get("/api/v1/state", [impl](const httplib::Request&, httplib::Response& res) {
    std::unique_lock lock(impl->mutex);
    std::string body = "{";
    for (auto event : kStateEvents) {
      auto json = impl->snapshot(event);
      if (!json) {
        lock.unlock();
        sendError(res, 503, "Vox 還在啟動");
        return;
      }
      body += jsonString(event) + ":" + *json + ",";
    }
    lock.unlock();
    body.back() = '}';
    res.set_content(body, kJsonType);
  });

  for (auto event : {std::string_view("lyrics"), std::string_view("queue")}) {
    server.Get("/api/v1/" + std::string(event), [impl, event](const httplib::Request&, httplib::Response& res) {
      std::unique_lock lock(impl->mutex);
      auto json = impl->snapshot(event);
      lock.unlock();
      if (json) res.set_content(*json, kJsonType);
      else sendError(res, 503, "Vox 還在啟動");
    });
  }

  server.Post("/api/v1/command", [weak](const httplib::Request& req, httplib::Response& res) {
    forwardAndWait(weak.lock(), "command", req, res);
  });
  server.Post("/api/v1/queue", [weak](const httplib::Request& req, httplib::Response& res) {
    forwardAndWait(weak.lock(), "enqueue", req, res);
  });

  server.Get("/api/v1/events", [impl](const httplib::Request&, httplib::Response& res) {
    auto subscriber = std::make_shared<Impl::Subscriber>();
    {
      std::lock_guard lock(impl->mutex);
      for (auto event : kEvents)
        if (auto json = impl->snapshot(event)) subscriber->frames.push_back(frameOf(event, *json));
      impl->subscribers.insert(subscriber);
    }
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
      "text/event-stream",
      [impl, subscriber](size_t, httplib::DataSink& sink) {
        std::unique_lock lock(impl->mutex);
        bool ready = impl->changed.wait_for(lock, kHeartbeat, [&] { return impl->closing || !subscriber->frames.empty(); });
        if (impl->closing) {
          lock.unlock();
          sink.done();
          return true;
        }
        std::string text;
        if (ready) {
          for (auto& frame : subscriber->frames) text += frame;
          subscriber->frames.clear();
        } else {
          text = kHeartbeatFrame;
        }
        lock.unlock();
        return sink.write(text.data(), text.size());
      },
      [impl, subscriber](bool) {
        std::lock_guard lock(impl->mutex);
        impl->subscribers.erase(subscriber);
      });
  });
}

ApiServer::~ApiServer() {
  {
    std::lock_guard lock(impl_->mutex);
    impl_->closing = true;
  }
  impl_->changed.notify_all();
  impl_->server.stop();
  if (impl_->listener.joinable()) impl_->listener.join();
}

std::wstring ApiServer::start() {
  if (!impl_->server.bind_to_port("127.0.0.1", kApiPort))
    return L"本機介面開不起來：port " + std::to_wstring(kApiPort) + L" 被別的程式占用";
  Impl* impl = impl_.get();
  impl->listener = std::thread([impl] { impl->server.listen_after_bind(); });
  return {};
}

void ApiServer::publish(const std::string& event, const std::string& json) {
  if (std::find(kEvents.begin(), kEvents.end(), event) == kEvents.end())
    throw winrt::hresult_invalid_argument(L"本機介面沒有這個事件：" + winrt::to_hstring(event));
  if (json.find_first_of("\r\n") != std::string::npos)
    throw winrt::hresult_invalid_argument(L"本機介面的事件內容不能換行：" + winrt::to_hstring(event));
  std::lock_guard lock(impl_->mutex);
  impl_->latest[event] = json;
  const std::string frame = frameOf(event, json);
  for (auto& subscriber : impl_->subscribers) subscriber->frames.push_back(frame);
  impl_->changed.notify_all();
}
