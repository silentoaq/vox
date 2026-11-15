#pragma once
#include <memory>
#include <string>
#include <vector>

inline constexpr int kSampleRate = 44100;
inline constexpr int kSources = 4;
inline constexpr int kVocals = 3;

struct Stems {
  std::vector<float> accompaniment[2];
  std::vector<float> vocals[2];
};

class Demucs {
public:
  Demucs(const std::wstring& modelPath, int threads);
  ~Demucs();
  int segment() const { return segment_; }
  Stems separate(const float* left, const float* right);

private:
  struct Runtime;
  std::unique_ptr<Runtime> runtime_;
  int segment_ = 0;
};
