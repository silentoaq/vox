#pragma once
#import <Foundation/Foundation.h>

inline constexpr int kServerPort = 47391;

class Server {
public:
  Server(NSString* uiDir, NSString* stemsDir);
  ~Server();
  NSString* start();

private:
  void accept(int listener);
  void serve(int client);

  NSString* uiDir_;
  NSString* stemsDir_;
  int listeners_[2] = {-1, -1};
};
