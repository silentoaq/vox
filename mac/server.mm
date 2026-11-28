#import "server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>

namespace {

constexpr size_t kHeaderLimit = 16 * 1024;
NSString* const kStemsPrefix = @"/stems/";

NSDictionary<NSString*, NSString*>* contentTypes() {
  static NSDictionary* types = @{
    @"html" : @"text/html; charset=utf-8",
    @"js" : @"text/javascript; charset=utf-8",
    @"css" : @"text/css; charset=utf-8",
    @"json" : @"application/json; charset=utf-8",
    @"png" : @"image/png",
    @"webp" : @"image/webp",
    @"svg" : @"image/svg+xml",
    @"ico" : @"image/x-icon",
    @"woff2" : @"font/woff2",
    @"ttf" : @"font/ttf",
    @"otf" : @"font/otf",
    @"opus" : @"audio/ogg",
    @"txt" : @"text/plain; charset=utf-8",
  };
  return types;
}

bool sendAll(int socket, const void* data, size_t size) {
  const char* bytes = static_cast<const char*>(data);
  while (size > 0) {
    const ssize_t sent = ::send(socket, bytes, size, 0);
    if (sent <= 0) return false;
    bytes += sent;
    size -= size_t(sent);
  }
  return true;
}

bool respond(int socket, int status, NSString* reason, NSString* type, NSData* body, bool head) {
  NSString* header = [NSString stringWithFormat:@"HTTP/1.1 %d %@\r\nContent-Type: %@\r\nContent-Length: %lu\r\nCache-Control: no-store\r\n"
                                                @"X-Content-Type-Options: nosniff\r\n\r\n",
                                                status, reason, type, (unsigned long)body.length];
  NSData* bytes = [header dataUsingEncoding:NSUTF8StringEncoding];
  if (!sendAll(socket, bytes.bytes, bytes.length)) return false;
  return head || sendAll(socket, body.bytes, body.length);
}

bool hostAllowed(const std::string& request) {
  const std::string expected = "\r\nhost: localhost:" + std::to_string(kServerPort) + "\r\n";
  std::string lower = request + "\r\n";
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  return lower.find(expected) != std::string::npos;
}

int bindLoopback(int family) {
  const int listener = socket(family, SOCK_STREAM, 0);
  if (listener < 0) return -1;
  const int on = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  setsockopt(listener, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
  int bound = -1;
  if (family == AF_INET) {
    sockaddr_in address{};
    address.sin_len = sizeof address;
    address.sin_family = AF_INET;
    address.sin_port = htons(kServerPort);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bound = bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address);
  } else {
    setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof on);
    sockaddr_in6 address{};
    address.sin6_len = sizeof address;
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(kServerPort);
    address.sin6_addr = in6addr_loopback;
    bound = bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address);
  }
  if (bound != 0 || listen(listener, SOMAXCONN) != 0) {
    close(listener);
    return -1;
  }
  return listener;
}

}

Server::Server(NSString* uiDir, NSString* stemsDir) : uiDir_(uiDir), stemsDir_(stemsDir) {}

Server::~Server() {
  for (int listener : listeners_)
    if (listener >= 0) close(listener);
}

NSString* Server::start() {
  const int families[] = {AF_INET, AF_INET6};
  for (int i = 0; i < 2; ++i) {
    listeners_[i] = bindLoopback(families[i]);
    if (listeners_[i] < 0) return [NSString stringWithFormat:@"localhost:%d 已經被別的程式占用（%s）", kServerPort, strerror(errno)];
  }
  for (int listener : listeners_) {
    std::thread([this, listener] { accept(listener); }).detach();
  }
  return nil;
}

void Server::accept(int listener) {
  for (;;) {
    const int client = ::accept(listener, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR || errno == ECONNABORTED) continue;
      return;
    }
    const int on = 1;
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    std::thread([this, client] {
      @autoreleasepool {
        serve(client);
      }
      close(client);
    }).detach();
  }
}

void Server::serve(int client) {
  std::string pending;
  char buffer[4096];
  for (;;) {
    size_t end;
    while ((end = pending.find("\r\n\r\n")) == std::string::npos) {
      if (pending.size() > kHeaderLimit) return;
      const ssize_t read = recv(client, buffer, sizeof buffer, 0);
      if (read <= 0) return;
      pending.append(buffer, size_t(read));
    }
    const std::string request = pending.substr(0, end);
    pending.erase(0, end + 4);
    const size_t firstSpace = request.find(' ');
    const size_t secondSpace = request.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) return;
    const std::string method = request.substr(0, firstSpace);
    const bool head = method == "HEAD";
    if (method != "GET" && !head) {
      respond(client, 405, @"Method Not Allowed", @"text/plain", [NSData data], false);
      return;
    }
    if (!hostAllowed(request)) {
      respond(client, 403, @"Forbidden", @"text/plain", [NSData data], false);
      return;
    }
    std::string target = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    target = target.substr(0, target.find_first_of("?#"));
    NSString* path = [@(target.c_str()) stringByRemovingPercentEncoding];
    NSString* root = uiDir_;
    if ([path hasPrefix:kStemsPrefix]) {
      root = stemsDir_;
      path = [path substringFromIndex:kStemsPrefix.length - 1];
    }
    if ([path isEqualToString:@"/"]) path = @"/index.html";
    NSData* body = nil;
    if (path && ![path.pathComponents containsObject:@".."]) body = [NSData dataWithContentsOfFile:[root stringByAppendingString:path]];
    const bool sent = body ? respond(client, 200, @"OK", contentTypes()[path.pathExtension.lowercaseString] ?: @"application/octet-stream", body, head)
                           : respond(client, 404, @"Not Found", @"text/plain", [NSData data], head);
    if (!sent) return;
  }
}
