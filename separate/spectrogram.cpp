#include "spectrogram.h"

#include <cmath>
#include <numbers>

#pragma warning(push)
#pragma warning(disable : 4324)
#include "pocketfft_hdronly.h"
#pragma warning(pop)

namespace {

constexpr int kPad = kHop / 2 * 3;
constexpr int kEdgeFrames = 2;

const std::vector<float>& window() {
  static const std::vector<float> w = [] {
    std::vector<float> values(kFft);
    for (int n = 0; n < kFft; ++n)
      values[n] = float(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / kFft));
    return values;
  }();
  return w;
}

int reflect(int i, int n) {
  if (i < 0) return -i;
  if (i >= n) return 2 * (n - 1) - i;
  return i;
}

}

int framesFor(int length) { return (length + kHop - 1) / kHop; }

void spectrogram(const float* channel, int length, std::vector<std::complex<float>>& out) {
  const int frames = framesFor(length);
  const int padded = frames * kHop + 2 * kPad;
  std::vector<float> first(padded);
  for (int i = 0; i < padded; ++i) first[i] = channel[reflect(i - kPad, length)];
  std::vector<float> centered(padded + kFft);
  for (int i = 0; i < padded + kFft; ++i) centered[i] = first[reflect(i - kFft / 2, padded)];

  const auto& w = window();
  std::vector<float> windowed(size_t(frames) * kFft);
  for (int t = 0; t < frames; ++t) {
    const float* frame = centered.data() + size_t(t + kEdgeFrames) * kHop;
    float* target = windowed.data() + size_t(t) * kFft;
    for (int n = 0; n < kFft; ++n) target[n] = frame[n] * w[n];
  }
  std::vector<std::complex<float>> spectra(size_t(frames) * (kBins + 1));
  pocketfft::r2c<float>({size_t(frames), size_t(kFft)}, {ptrdiff_t(kFft * sizeof(float)), ptrdiff_t(sizeof(float))},
                        {ptrdiff_t((kBins + 1) * sizeof(std::complex<float>)), ptrdiff_t(sizeof(std::complex<float>))}, 1,
                        pocketfft::FORWARD, windowed.data(), spectra.data(), 1.0f / std::sqrt(float(kFft)));
  out.resize(size_t(kBins) * frames);
  for (int t = 0; t < frames; ++t)
    for (int bin = 0; bin < kBins; ++bin) out[size_t(bin) * frames + t] = spectra[size_t(t) * (kBins + 1) + bin];
}

void inverseSpectrogram(const std::complex<float>* spec, int length, float* out) {
  const int frames = framesFor(length);
  const int allFrames = frames + 2 * kEdgeFrames;
  std::vector<std::complex<float>> spectra(size_t(frames) * (kBins + 1));
  for (int t = 0; t < frames; ++t)
    for (int bin = 0; bin < kBins; ++bin) spectra[size_t(t) * (kBins + 1) + bin] = spec[size_t(bin) * frames + t];
  std::vector<float> signal(size_t(frames) * kFft);
  pocketfft::c2r<float>({size_t(frames), size_t(kFft)},
                        {ptrdiff_t((kBins + 1) * sizeof(std::complex<float>)), ptrdiff_t(sizeof(std::complex<float>))},
                        {ptrdiff_t(kFft * sizeof(float)), ptrdiff_t(sizeof(float))}, 1, pocketfft::BACKWARD,
                        spectra.data(), signal.data(), 1.0f / std::sqrt(float(kFft)));

  const auto& w = window();
  const size_t total = size_t(kFft) + size_t(kHop) * (allFrames - 1);
  std::vector<float> sum(total, 0.0f);
  std::vector<float> envelope(total, 0.0f);
  for (int f = 0; f < allFrames; ++f) {
    const size_t start = size_t(f) * kHop;
    const bool content = f >= kEdgeFrames && f < kEdgeFrames + frames;
    const float* frame = content ? signal.data() + size_t(f - kEdgeFrames) * kFft : nullptr;
    for (int n = 0; n < kFft; ++n) {
      if (content) sum[start + n] += frame[n] * w[n];
      envelope[start + n] += w[n] * w[n];
    }
  }
  const size_t offset = kFft / 2 + kPad;
  for (int i = 0; i < length; ++i) {
    const float e = envelope[offset + i];
    out[i] = e > 1e-11f ? sum[offset + i] / e : 0.0f;
  }
}
