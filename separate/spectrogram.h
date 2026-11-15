#pragma once
#include <complex>
#include <vector>

inline constexpr int kFft = 4096;
inline constexpr int kHop = 1024;
inline constexpr int kBins = kFft / 2;

int framesFor(int length);
void spectrogram(const float* channel, int length, std::vector<std::complex<float>>& out);
void inverseSpectrogram(const std::complex<float>* spec, int length, float* out);
