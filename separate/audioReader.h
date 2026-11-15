#pragma once
#include <windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objidl.h>
#include <wrl.h>

#include <cstdint>
#include <vector>

struct Audio {
  std::vector<float> left;
  std::vector<float> right;
};

class AudioReader {
public:
  AudioReader(IStream* stream, int sampleRate);
  ~AudioReader();
  int64_t estimatedSamples() const { return estimated_; }
  bool readMore(Audio& audio);

private:
  Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
  int64_t estimated_ = 0;
};
