#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

struct Audio {
  std::vector<float> left;
  std::vector<float> right;
};

class AudioReader {
public:
  virtual ~AudioReader() = default;
  virtual int64_t estimatedSamples() const = 0;
  virtual bool readMore(Audio& audio) = 0;
};

std::unique_ptr<AudioReader> openInputAudio(const std::filesystem::path& savePath, int sampleRate);
